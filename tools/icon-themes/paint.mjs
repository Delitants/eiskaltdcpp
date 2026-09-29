export const themes = {
  prism: {
    label: 'Prism', style: 'Glossy colorful Phosphor geometry with shaded bodies, dark rims, and highlights',
    reference: 'exec-440dcfaa-0a76-4733-9304-6b8409134269.png',
    palettes: {
      light: { primary: '#009cf0', blue: '#008ded', teal: '#00c2b3', green: '#89d900', gold: '#ffc400', red: '#f63a30', muted: '#8c9da6', gear: '#596b77', tray: '#52636d' },
      dark: { primary: '#23b8ff', blue: '#20a6ff', teal: '#21dfca', green: '#a0ef12', gold: '#ffd42a', red: '#ff5444', muted: '#b4c9d6', gear: '#768994', tray: '#70848f' },
    },
  },
  contour: {
    label: 'Contour', style: 'Light 12-unit outline geometry, slate with restrained teal and semantic accents',
    reference: 'exec-a63e43ba-2f97-4122-8565-d5285c7968b8.png',
    palettes: {
      light: { primary: '#344653', blue: '#42758f', teal: '#148590', green: '#268969', gold: '#c7891b', red: '#d5554e', muted: '#6b7c88' },
      dark: { primary: '#e2eaf0', blue: '#8dbfeb', teal: '#76cfd0', green: '#77cda8', gold: '#f0ba59', red: '#ff857a', muted: '#adbfcd' },
    },
  },
  slate: {
    label: 'Slate', style: 'Filled navy silhouettes on light and light-gray silhouettes on dark, with muted accents',
    reference: 'exec-da1e46b1-8037-4a4b-bf4c-a0347326f36b.png',
    palettes: {
      light: { primary: '#20344b', blue: '#557e9f', teal: '#508e96', green: '#4d9278', gold: '#d69d32', red: '#cf675e', muted: '#657586' },
      dark: { primary: '#dce4ec', blue: '#8cb0cf', teal: '#8abfc2', green: '#91c4ac', gold: '#e7b75b', red: '#e99285', muted: '#a6b7c6' },
    },
  },
  reborn: {
    label: 'Reborn', style: 'Bright blue, teal, green, and amber filled silhouettes on both surfaces',
    reference: 'exec-fb6e5dbc-0cc3-4aa8-aeed-cc8ac7ccbda0.png',
    palettes: {
      light: { primary: '#1486f8', blue: '#1486f8', teal: '#0cb5ab', green: '#21bb67', gold: '#f7ad09', red: '#ff5b53', muted: '#72a9c4' },
      dark: { primary: '#39a2ff', blue: '#39a2ff', teal: '#30d2c2', green: '#3cdb7a', gold: '#ffc229', red: '#ff7968', muted: '#91cbd9' },
    },
  },
};

const mix = (hex, target, amount) => '#' + [1, 3, 5].map(index => Math.round(
  parseInt(hex.slice(index, index + 2), 16) * (1 - amount) + target * amount,
).toString(16).padStart(2, '0')).join('');

function paintSpec(role, palette, theme) {
  const color = name => palette[name] ?? (['gear', 'tray'].includes(name) ? palette.primary : null);
  if (color(role)) return { color: color(role) };
  const split = (axis, values) => ({ axis, stops: values.map(([offset, name]) => [offset, color(name)]) });
  switch (role) {
    case 'torrent-ring': return ['prism', 'reborn'].includes(theme)
      ? split('x', [[0, 'green'], [0.7, 'green'], [0.7, 'blue'], [1, 'blue']])
      : split('x', [[0, 'primary'], [0.7, 'primary'], [0.7, 'teal'], [1, 'teal']]);
    case 'transfers': return split('x', [[0, 'green'], [0.5, 'green'], [0.5, 'blue'], [1, 'blue']]);
    case 'download': return split('y', [[0, 'green'], [0.66, 'green'], [0.66, 'primary'], [1, 'primary']]);
    case 'upload': return split('y', [[0, 'blue'], [0.66, 'blue'], [0.66, 'teal'], [1, 'teal']]);
    case 'refresh': return split('y', [[0, 'primary'], [0.5, 'primary'], [0.5, 'green'], [1, 'green']]);
    case 'people': return split('x', [[0, 'primary'], [0.62, 'primary'], [0.62, 'teal'], [1, 'teal']]);
    case 'person': return split('y', [[0, 'primary'], [0.78, 'primary'], [0.78, 'teal'], [1, 'teal']]);
    case 'sliders': return split('y', [[0, 'primary'], [0.5, 'primary'], [0.5, 'teal'], [1, 'teal']]);
    case 'network': return split('x', [[0, 'primary'], [0.31, 'primary'], [0.31, 'teal'], [0.55, 'teal'], [0.55, 'primary'], [1, 'primary']]);
    case 'globe': return theme === 'prism' ? { color: palette.green }
      : split('x', [[0, 'primary'], [0.37, 'primary'], [0.37, 'teal'], [0.63, 'teal'], [0.63, 'primary'], [1, 'primary']]);
    case 'magnet': return split('x', [[0, 'red'], [0.5, 'red'], [0.5, 'blue'], [1, 'blue']]);
    case 'plug': return split('diagonal', [[0, 'primary'], [0.57, 'primary'], [0.57, 'teal'], [0.63, 'teal'], [0.63, 'primary'], [1, 'primary']]);
    default: throw new Error(`Unknown paint role: ${role}`);
  }
}

function gradient(id, axis, stops) {
  const vector = axis === 'x' ? 'x1="0" y1="0" x2="256" y2="0"'
    : axis === 'diagonal' ? 'x1="0" y1="256" x2="256" y2="0"'
      : 'x1="0" y1="0" x2="0" y2="256"';
  return `<linearGradient id="${id}" gradientUnits="userSpaceOnUse" ${vector}>${stops.map(([offset, color, opacity = 1]) =>
    `<stop offset="${offset}" stop-color="${color}" stop-opacity="${opacity}"/>`).join('')}</linearGradient>`;
}

export function material(role, theme, surface, id) {
  const palette = themes[theme].palettes[surface];
  const spec = paintSpec(role, palette, theme);
  const defs = [];
  let fill = spec.color;
  if (spec.stops) {
    defs.push(gradient(`${id}-color`, spec.axis, spec.stops));
    fill = `url(#${id}-color)`;
  }
  if (theme !== 'prism') return { defs, fill };

  const base = spec.color ?? palette.primary;
  if (spec.color) {
    defs.push(gradient(`${id}-body`, 'y', [
      [0, mix(base, 255, 0.64)], [0.22, mix(base, 255, 0.24)],
      [0.5, base], [0.76, mix(base, 0, 0.08)], [1, mix(base, 0, 0.38)],
    ]));
    fill = `url(#${id}-body)`;
  }
  defs.push(gradient(`${id}-rim`, 'y', [[0, mix(base, 0, 0.6)], [0.5, mix(base, 0, 0.75)], [1, mix(base, 0, 0.83)]]));
  // Duotone foregrounds can be the entire paper/user outline, not just a rim.
  if (surface === 'dark') {
    defs.push(gradient(`${id}-ink`, 'y', [[0, '#effaff'], [0.45, '#c3deea'], [1, '#88adbe']]));
  }
  defs.push(gradient(`${id}-shine`, 'y', [
    [0, '#ffffff', 0.65], [0.25, '#ffffff', 0.28], [0.46, '#ffffff', 0.08],
    [0.49, '#ffffff', 0], [1, '#ffffff', 0],
  ]));
  return { defs, fill, rim: `url(#${id}-rim)`,
    ink: surface === 'dark' ? `url(#${id}-ink)` : `url(#${id}-rim)`, shine: `url(#${id}-shine)` };
}
