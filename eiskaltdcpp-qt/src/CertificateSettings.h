#pragma once

#include <QWidget>
#include <QTemporaryDir>
#include <memory>
#include <vector>

class QLineEdit;
class QLabel;
class QPushButton;
namespace dcpp { class SettingsManager; }

class CertificateSettings final : public QWidget {
    Q_OBJECT
public:
    CertificateSettings(dcpp::SettingsManager& settings, const QString& cid,
                        const QString& directory, QWidget* parent = nullptr);
    ~CertificateSettings() override;
    QString validationError() const;
    void save();

private:
    void updateDetails();
    void generate();
    dcpp::SettingsManager& settings;
    QString cid, directory, originalCertificate, originalKey, originalTrust;
    QLineEdit *certificate, *key, *trust;
    QLabel *details, *message;
    QPushButton* generateButton;
    QWidget* editors;
    bool busy = false;
    std::vector<std::unique_ptr<QTemporaryDir>> generated;
};
