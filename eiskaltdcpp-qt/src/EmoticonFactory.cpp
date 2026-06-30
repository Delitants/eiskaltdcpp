/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/
/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 */

#include "EmoticonFactory.h"
#include "WulforSettings.h"
#include "WulforUtil.h"
#include "QtContext.h"
#include "QtContextAware.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QtDebug>
#include <QApplication>
#include <QLabel>

#include <math.h>

static const QString EmoticonSectionName = "emoticons-map";
static const QString EmoticonSubsectionName = "emoticon";
static const QString EmoticonTextSectionName = "name";

EmoticonFactory::EmoticonFactory(dcpp::DCContext& ctx) :
    QtContextAware(ctx),
    QObject(nullptr)
{
}

EmoticonFactory::~EmoticonFactory(){
    clear();
}

void EmoticonFactory::load(){
    const QString configuredTheme = qtCtx()->settings()->getStr(WS_APP_EMOTICON_THEME, "default").trimmed();
    const QString emoticonsBasePath = qtCtx()->wulforUtil()->getEmoticonsPath();

    auto themeUsable = [&emoticonsBasePath](const QString &theme) -> bool {
        if (theme.isEmpty())
            return false;
        return QDir(emoticonsBasePath + theme).exists() &&
               QFile::exists(emoticonsBasePath + theme + ".xml");
    };

    QString resolvedTheme = configuredTheme;
    if (!themeUsable(resolvedTheme))
        resolvedTheme = QStringLiteral("default");

    if (!themeUsable(resolvedTheme)) {
        QDir dir(emoticonsBasePath);
        const QStringList candidates = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &candidate : candidates) {
            if (themeUsable(candidate)) {
                resolvedTheme = candidate;
                break;
            }
        }
    }

    if (!themeUsable(resolvedTheme))
        return;

    if (currentTheme == resolvedTheme && !list.isEmpty())
        return;

    const QString xmlFile = emoticonsBasePath + resolvedTheme + ".xml";

    QFile f(xmlFile);

    if (!f.open(QIODevice::ReadOnly))
        return;

    clear();
    currentTheme = resolvedTheme;

    QDomDocument dom;
    QString err_msg = "";
    int err_line = 0, err_col = 0;

    const bool parsed = dom.setContent(&f, false, &err_msg, &err_line, &err_col);
    if (parsed)
        createEmoticonMap(dom);
    else{
        qDebug() << err_line << ":" << err_col << " " << err_msg;
    }

    f.close();

    for (const auto &d : docs)
        addEmoticons(d);
}

void EmoticonFactory::addEmoticons(QTextDocument *to){
    if (list.isEmpty() || !to)
        return;

    const QString emoTheme = currentTheme.isEmpty()
        ? qtCtx()->settings()->getStr(WS_APP_EMOTICON_THEME, "default")
        : currentTheme;

    for (const auto &i : list){
        to->addResource( QTextDocument::ImageResource,
                         QUrl(emoTheme + "/emoticon" + QString().setNum(i->id)),
                         i->pixmap.toImage()
                       );
    }

    if (!docs.contains(to)){
        connect(to, &QTextDocument::destroyed, this, &EmoticonFactory::slotDocDeleted);

        docs << to;
    }
}

QString EmoticonFactory::convertEmoticons(const QString &html){
    if (html.isEmpty() || list.isEmpty() || map.isEmpty())
        return html;

    const QString emoTheme = currentTheme.isEmpty()
        ? qtCtx()->settings()->getStr(WS_APP_EMOTICON_THEME, "default")
        : currentTheme;
    QString out = "";
    QString buf = html;

    auto it = map.end();
    auto begin = map.begin();

    bool force_emot = qtCtx()->settings()->getBool(WB_APP_FORCE_EMOTICONS);

    if (!force_emot){
        buf.prepend(" ");
        buf.append(" ");
    }

    while (!buf.isEmpty()){
        if (buf.startsWith("<a href=") && buf.indexOf("</a>") > 0){
            QString add = buf.left(buf.indexOf("</a>")) + "</a>";

            out += add;
            buf.remove(0, add.length());

            continue;
        }

        bool found = false;

        for (it = std::prev(map.end()); ; --it){
            if (force_emot){
                if (buf.startsWith(it.key())){
                    EmoticonObject *obj = it.value();

                    QString img = QString("<img alt=\"%1\" title=\"%1\" align=\"center\" source=\"%2/emoticon%3\" />")
                                  .arg(it.key())
                                  .arg(emoTheme)
                                  .arg(obj->id);

                    out += img + " ";
                    buf.remove(0, it.key().length());

                    found = true;

                    break;
                }
            }
            else{
                if (buf.startsWith(" "+it.key()+" ")){
                    EmoticonObject *obj = it.value();

                    QString img = QString(" <img alt=\"%1\" title=\"%1\" align=\"center\" source=\"%2/emoticon%3\" /> ")
                                  .arg(it.key())
                                  .arg(emoTheme)
                                  .arg(obj->id);

                    out += img;
                    buf.remove(0, it.key().length()+1);

                    found = true;

                    break;
                }
                else if (buf.startsWith(" "+it.key()+"\n")){
                    EmoticonObject *obj = it.value();

                    QString img = QString(" <img alt=\"%1\" title=\"%1\" align=\"center\" source=\"%2/emoticon%3\" />\n")
                                  .arg(it.key())
                                  .arg(emoTheme)
                                  .arg(obj->id);

                    out += img;
                    buf.remove(0, it.key().length()+2);

                    found = true;

                    break;
                }
            }
        }

        if (!found){
            out += buf.at(0);

            buf.remove(0, 1);
        }
        if (it == begin) break;
    }

    if (!force_emot){
        if (out.startsWith(" "))
            out.remove(0, 1);
        if (out.endsWith(" "))
            out.remove(out.length()-1, 1);
    }

    return out;
}

void EmoticonFactory::createEmoticonMap(const QDomNode &root){
    if (root.isNull())
        return;

    QDomNode r = findSectionByName(root, EmoticonSectionName);

    if (r.isNull())
        return;

    DomNodeList emoNodes;
    getSubSectionsByName(r, emoNodes, EmoticonSubsectionName);

    if (emoNodes.isEmpty())
        return;

    clear();

    for (const auto &node : emoNodes){
        const QString emoTheme = currentTheme.isEmpty()
            ? qtCtx()->settings()->getStr(WS_APP_EMOTICON_THEME, "default")
            : currentTheme;

        EmoticonObject *emot = new EmoticonObject();
        QDomElement el = node.toElement();

        emot->fileName  = el.attribute("file").toUtf8();
        emot->id        = list.size();
        emot->pixmap    = QPixmap();
        emot->pixmap.load(qtCtx()->wulforUtil()->getEmoticonsPath() +
                          emoTheme + QDir::separator() + emot->fileName);

        DomNodeList emoTexts;
        getSubSectionsByName(node, emoTexts, EmoticonTextSectionName);

        if (emoTexts.isEmpty()){
            delete emot;

            continue;
        }

        int registered = 0;
        for (const auto &node : emoTexts){
            QDomElement el = node.toElement();

            if (el.isNull())
                continue;

            QString text = el.attribute("text").toUtf8();

            if (text.isEmpty() || map.contains(text))
                continue;

            registered++;

            map.insert(text, emot);
        }

        if (registered > 0)
            list.push_back(emot);
        else
            delete emot;
    }
}

void EmoticonFactory::fillLayout(QLayout *l, QSize &recommendedSize){
    if (!l)
        return;

    int w = 0, h = 0, total = list.size();

    if (!total){
        recommendedSize = QSize(50, 50);
        return;
    }

    // Ensure list is valid before iterating
    if (list.isEmpty()) {
        recommendedSize = QSize(50, 50);
        return;
    }

    for (const auto &i : list){
        EmoticonLabel *lbl = new EmoticonLabel();
        const int targetPx = 28;
        QPixmap icon = i->pixmap;
        if (!icon.isNull() && (icon.width() > targetPx || icon.height() > targetPx))
            icon = icon.scaled(targetPx, targetPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        lbl->setPixmap(icon);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setFixedSize(icon.size() + QSize(4, 4));
        lbl->setContentsMargins(0, 0, 0, 0);
        lbl->setToolTip(map.keys(i).first());

        w += lbl->width();
        h  = lbl->height();

        l->addWidget(lbl);
    }

    int square = w*h;
    int dim = static_cast<int>(sqrt(square));
    int extra = (dim/total);//for margins

    //10 extra pixels
    recommendedSize.setHeight(dim+extra+10);
    recommendedSize.setWidth(dim+extra+10);
}

void EmoticonFactory::clear(){
    qDeleteAll(list);

    map.clear();
    list.clear();
}

QDomNode EmoticonFactory::findSectionByName(const QDomNode &node, const QString &name){
    QDomNode domNode = node.firstChild();

    while (!domNode.isNull()){
        if (domNode.isElement()){
            QDomElement domElement = domNode.toElement();

            if (!domElement.isNull() && domElement.tagName().toLower() == name.toLower())
                return domNode;
        }

        domNode = domNode.nextSibling();
    }

    return QDomNode();
}
void EmoticonFactory::getSubSectionsByName(const QDomNode &node, EmoticonFactory::DomNodeList &list, const QString &name){
    Q_UNUSED(name)

    QDomNode domNode = node.firstChild();

    while (!domNode.isNull()){
        list << domNode;

        domNode = domNode.nextSibling();
    }
}

void EmoticonFactory::slotDocDeleted(){
    QTextDocument *doc = reinterpret_cast<QTextDocument*>(sender());

    if (docs.contains(doc))
        docs.removeAt(docs.indexOf(doc));
}
