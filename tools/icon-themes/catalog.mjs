export const themeNames = ['prism', 'contour', 'slate', 'reborn'];
export const excludedStems = ['icon_appl', 'icon_appl_big', 'icon_msg', 'icon_msg_big',
  'qt-logo', 'toolbar20', 'toolbar20-highlight'];
export const additionalStems = ['document-new', 'media-pause', 'media-play',
  'settings-advanced', 'settings-notifications', 'settings-sharing', 'settings-user-commands', 'sliders', 'torrent'];

const component = (icon, paint = 'primary', options = {}) => ({ icon, paint, ...options });
const line = { prism: 'bold', slate: 'bold', reborn: 'bold' };
const body = (icon, paint = 'primary', options = {}) => component(icon, paint, options);
const stroke = (icon, paint = 'primary', options = {}) => component(icon, paint, { weights: line, ...options });
const badge = (icon, paint) => component(icon, paint, {
  transform: [0.43, 0, 0, 0.43, 141, 139], badge: true,
  weights: { contour: 'regular' },
});
const badged = (icon, symbol, paint = 'primary', accent = 'gold', options = {}) => [
  body(icon, paint, { transform: [0.74, 0, 0, 0.74, 2, 0], ...options }), badge(symbol, accent),
];
const directions = [component('arrows-down-up', 'transfers', { weights: { prism: 'fill', slate: 'fill', reborn: 'fill' } }),
  component('arrows-down-up', 'transfers', { weights: { prism: 'bold', slate: 'bold', reborn: 'bold' }, only: ['prism', 'slate', 'reborn'] })];
const tray = direction => [
  component(`${direction}-simple`, direction, { weights: { prism: 'fill' },
    paints: { prism: 'tray', slate: direction === 'download' ? 'primary' : 'teal', reborn: direction === 'download' ? 'primary' : 'teal' } }),
  component(`${direction}-simple`, direction === 'download' ? 'primary' : 'teal', {
    paints: { prism: 'tray' }, weights: { prism: 'bold', slate: 'bold', reborn: 'bold' }, only: ['prism', 'slate', 'reborn'] }),
  component(`arrow-fat-${direction === 'download' ? 'down' : 'up'}`, direction === 'download' ? 'green' : 'blue', {
    only: ['prism', 'slate', 'reborn'], transform: [0.6, 0, 0, 0.65, 51.2, direction === 'download' ? 0 : 10] }),
];
const queuedDownload = [
  body('list-bullets', 'primary', { weights: line, transform: [0.75, 0, 0, 0.75, 0, 0] }),
  { ...badge('arrow-fat-down', 'green'), icons: { contour: 'arrow-down' },
    transform: [0.4375, 0, 0, 0.4375, 140, 136] },
];
const completedDownload = [
  body('tray', 'primary', { transform: [0.75, 0, 0, 0.75, 0, 0] }),
  { ...badge('check-circle', 'green'), transform: [0.4375, 0, 0, 0.4375, 140, 136] },
];
const arrow = (direction, paint) => component(`arrow-fat-${direction}`, paint, {
  icons: { contour: `arrow-${direction}` },
});
const refresh = [component('arrows-clockwise', 'refresh', { weights: { prism: 'fill' } }),
  component('arrows-clockwise', 'refresh', { weights: line, only: ['prism', 'slate', 'reborn'] })];
const hub = component('globe', 'globe', {
  weights: { slate: 'bold', reborn: 'bold' },
  icons: { prism: 'globe-hemisphere-west' },
});
// Regular outlines compensate optically for the smaller Contour keycaps.
const shortcutKeys = [
  ['up', 79.36, 39], ['left', -0.6, 124], ['down', 79.36, 124], ['right', 159.32, 124],
].map(([direction, x, y]) => component(`arrow-square-${direction}`, 'primary', {
  transform: [0.38, 0, 0, 0.38, x, y], weights: { contour: 'regular' },
}));
const torrent = [
  // The fill/duotone variants are disks; bold retains six separate ring segments.
  component('circle-dashed', 'torrent-ring', { weights: line }),
  component('arrow-down', 'green', { only: ['contour'], transform: [0.625, 0, 0, 0.625, 48, 48] }),
  component('arrow-fat-down', 'green', { only: ['prism', 'slate', 'reborn'], transform: [0.5, 0, 0, 0.5, 64, 60] }),
];
const entries = [
  ['adls', 'Automatic directory-list search', badged('folder-simple', 'magnifying-glass', 'primary', 'teal')],
  ['application-exit', 'Quit application', [stroke('power', 'red')]],
  ['application-x-archive', 'Archive file', [body('file-archive', 'gold')]],
  ['application-x-executable', 'Executable file', [body('app-window', 'primary')]],
  ['audio-x-generic', 'Audio file', [body('music-notes', 'teal')]],
  ['ball_green', 'Online or granted slot', [body('circle', 'green')]],
  ['bookmark-new', 'Add bookmark or favorite', badged('bookmark-simple', 'plus-circle', 'gold', 'green')],
  ['chat', 'Group chat', [body('chats-circle', 'teal')]],
  ['configure', 'Preferences', [body('gear', 'gear'), component('circle', 'green', {
    only: ['prism'], transform: [0.28, 0, 0, 0.28, 92.16, 92.16], weights: { prism: 'duotone' },
  })]],
  ['console', 'Console', [body('terminal-window')]],
  ['dialog-close', 'Close dialog or tab', [stroke('x')]],
  ['document-edit', 'Edit document', [body('note-pencil')]],
  ['document-new', 'Create document or torrent', [body('file-plus', 'primary')]],
  ['download', 'Download queue', queuedDownload],
  ['edit-clear', 'Clear entries', [body('broom', 'red')]],
  ['edit-clear-locationbar-rtl', 'Clear text field', [body('backspace', 'red')]],
  ['edit-copy', 'Copy', [body('copy')]],
  ['edit-delete', 'Delete', [body('trash', 'red')]],
  ['edit-find', 'Find in document', [body('file-magnifying-glass')]],
  ['eraser', 'Erase', [body('eraser', 'red')]],
  ['face-smile', 'Emoticons', [body('smiley', 'gold')]],
  ['fav', 'Favorites', [body('star', 'gold')]],
  ['favadd', 'Add to favorites', badged('star', 'plus-circle', 'gold', 'green')],
  ['favrem', 'Remove from favorites', badged('star', 'minus-circle', 'gold', 'red')],
  ['favserver', 'Favorite hubs', badged('globe', 'star', 'globe', 'gold', { weights: { slate: 'bold', reborn: 'bold' } })],
  ['favusers', 'Favorite users', badged('users', 'star', 'people', 'gold')],
  ['find', 'Search', [stroke('magnifying-glass')]],
  ['folder-blue', 'Folder', [body('folder-simple')]],
  ['freespace', 'Free disk space', [body('hard-drive', 'teal')]],
  ['go-down', 'Move down or download direction', [arrow('down', 'green')]],
  ['go-down-search', 'Finished downloads', completedDownload],
  ['go-next', 'Next', [arrow('right', 'primary')]],
  ['go-previous', 'Previous', [arrow('left', 'primary')]],
  ['go-top', 'Move to top', [stroke('arrow-line-up', 'primary')]],
  ['go-up', 'Move up or upload direction', [arrow('up', 'blue')]],
  ['go-up-search', 'Finished uploads', tray('upload')],
  ['gui', 'Windows and layout', [body('browsers')]],
  ['hashing', 'Hashing progress', [stroke('hash', 'teal')]],
  ['history', 'History', [stroke('clock-counter-clockwise', 'teal')]],
  ['hubmsg', 'New hub message', badged('globe', 'chat-circle-dots', 'globe', 'gold', { weights: { slate: 'bold', reborn: 'bold' } })],
  ['im-user-away', 'Set away status', badged('user', 'moon', 'primary', 'gold')],
  ['image-x-generic', 'Image file', [body('image', 'teal')]],
  ['list-add', 'Add item', [stroke('plus', 'green')]],
  ['log_file', 'Live log', [body('file-text')]],
  ['magnet', 'Magnet link', [body('magnet', 'magnet')]],
  ['media-pause', 'Pause torrent or transfer', [body('pause', 'gold')]],
  ['media-play', 'Start or resume torrent or transfer', [body('play', 'green')]],
  ['message', 'Message', [body('envelope-simple', 'teal')]],
  ['network-connect', 'Quick connect', [body('plug', 'plug'), component('lightning', 'green', {
    only: ['prism'], transform: [0.4, 0, 0, 0.4, 142, 138],
  })]],
  ['network-disconnect', 'Disconnect', [body('plugs', 'red')]],
  ['openlist', 'Open file list', badged('folder-open', 'list-bullets', 'primary', 'teal')],
  ['own_filelist', 'Own file list', [body('list-bullets', 'teal', { weights: line })]],
  ['plugin', 'Extensions', [body('puzzle-piece', 'teal')]],
  ['pmmsg', 'New private message', badged('chat-circle-dots', 'lock-key', 'primary', 'gold')],
  ['queued-users', 'Queued upload users', [body('user-list', 'people')]],
  ['queued-users-highlight', 'Queued upload users needing attention', badged('user-list', 'bell', 'people', 'gold')],
  ['reconnect', 'Reconnect to hub', [body('plugs-connected', 'plug')]],
  ['refrlist', 'Refresh shared files', refresh],
  ['reload', 'Reload', [stroke('arrow-clockwise', 'teal')]],
  ['server', 'Public hubs', [component('circle', 'primary', { only: ['prism'] }), hub]],
  ['settings-advanced', 'Advanced settings', [body('sliders-horizontal', 'sliders')]],
  ['settings-connection', 'Connection settings', [body('share-network', 'network')]],
  ['settings-downloads', 'Download settings', tray('download')],
  ['settings-gui', 'Interface settings', [body('desktop', 'primary')]],
  ['settings-main', 'Main settings', [body('user', 'person')]],
  ['settings-notifications', 'Notification settings', [body('bell', 'gold')]],
  ['settings-sharing', 'Sharing settings', badged('folder-simple', 'arrow-up-right', 'primary', 'green', { weights: { prism: 'duotone' } })],
  ['settings-shortcuts', 'Keyboard shortcuts', shortcutKeys],
  ['settings-user-commands', 'User commands', [body('terminal-window', 'teal')]],
  ['sliders', 'Torrent options', [body('sliders-horizontal', 'sliders')]],
  ['slow', 'Speed limit enabled', badged('gauge', 'lock-key', 'gold', 'gold', { weights: line })],
  ['slow_off', 'Speed limit disabled', badged('gauge', 'lightning', 'primary', 'green', { weights: line })],
  ['spam', 'Block spam', [body('prohibit', 'red', { weights: line })]],
  ['spy', 'Search spy', [body('detective')]],
  ['status', 'Status messages', [body('info', 'teal')]],
  ['text-x-generic', 'Text document', [body('file-text', 'teal')]],
  ['torrent', 'Torrent swarm downloads', torrent],
  ['transfer', 'Downloads and uploads', directions],
  ['transfer-highlight', 'Transfers needing attention', [
    ...directions.map(part => ({ ...part, transform: [0.78, 0, 0, 0.78, 0, 0] })), badge('bell', 'gold'),
  ]],
  ['unknown', 'Unknown file', [body('file-dashed', 'muted')]],
  ['users', 'Users', [body('users', 'people')]],
  ['video-x-generic', 'Video file', [body('film-strip', 'primary')]],
  ['view-close', 'Hide window', [body('minus-square')]],
  ['view-filter', 'Filter', [body('funnel', 'teal')]],
  ['zoom-in', 'Zoom in', [stroke('magnifying-glass-plus')]],
  ['zoom-out', 'Zoom out', [stroke('magnifying-glass-minus')]],
];

export const icons = entries.map(([stem, title, components]) => ({ stem, title, components }))
  .sort((a, b) => a.stem < b.stem ? -1 : a.stem > b.stem ? 1 : 0);

export function resolveComponent(part, theme) {
  if (part.only && !part.only.includes(theme)) return null;
  const icon = part.icons?.[theme] ?? part.icon;
  const weight = part.weights?.[theme] ?? (theme === 'contour' ? 'light' : theme === 'prism' ? 'duotone' : 'fill');
  return { icon, weight, source: `assets/${weight}/${icon}${weight === 'regular' ? '' : `-${weight}`}.svg`,
    paint: part.paints?.[theme] ?? part.paint, transform: part.transform ?? [1, 0, 0, 1, 0, 0], badge: part.badge ?? false };
}

export function neededSources() {
  return [...new Set(icons.flatMap(icon => themeNames.flatMap(theme => icon.components
    .map(part => resolveComponent(part, theme)).filter(Boolean).map(part => part.source))))].sort();
}
