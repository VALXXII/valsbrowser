// Official @phosphor-icons/core 2.1.1 Bold, MIT. Build-time resvg only.
const fs = require('node:fs');
const path = require('node:path');
const {Resvg} = require('../tools/render/node_modules/@resvg/resvg-js');
const root = path.join(__dirname, '../assets/icons');
const mapping={plus:'plus',minimize:'minus',maximize:'square',close:'x',back:'arrow-left',forward:'arrow-right',reload:'arrow-counter-clockwise',copy:'link',downloads:'download-simple',private:'detective',settings:'gear-six','tab-close':'x',check:'check',favicon:'globe-simple',pause:'pause',resume:'play',open:'play-circle',folder:'folder-open','folder-closed':'folder','arrow-down':'arrow-down',timer:'timer','check-circle':'check-circle',warning:'warning-circle',retry:'arrow-clockwise','detail-pause':'pause','detail-close':'x',caret:'caret-down',lock:'lock-simple','lock-open':'lock-simple-open',danger:'warning',search:'magnifying-glass',file:'file',star:'star',overflow:'caret-double-right',tabs:'tabs',bookmarks:'bookmarks-simple','folder-plus':'folder-plus',edit:'pencil-simple',trash:'trash','open-out':'arrow-square-out'};
// Detail-line glyphs sit beside 14 px text.
const small=['arrow-down','timer','check-circle','warning','detail-pause','detail-close'];
for (const [alias,source] of Object.entries(mapping)) {
  const official=fs.readFileSync(path.join(root,'phosphor-bold',source+'-bold.svg'),'utf8');
  const svg=official.replace('fill="currentColor"','fill="#FFFFFF" opacity="0.5"');
  fs.writeFileSync(path.join(root, alias+'.svg'), svg);
  for(const dpi of [96,120,144,168,192,240,288,384]) {
    const px = Math.round((['minimize','maximize','close'].includes(alias)||small.includes(alias)?14:16)*dpi/96);
    const png = new Resvg(svg,{fitTo:{mode:'width',value:px}}).render().asPng();
    fs.writeFileSync(path.join(root, `${alias}-${dpi}.png`),png);
  }
}
// Mask-only close glyph (full opacity): the window close hover tints it to an exact alpha.
{const svg=fs.readFileSync(path.join(root,'phosphor-bold','x-bold.svg'),'utf8').replace('fill="currentColor"','fill="#FFFFFF"');
  fs.writeFileSync(path.join(root,'close-mask.svg'),svg);
  for(const dpi of [96,120,144,168,192,240,288,384])fs.writeFileSync(path.join(root,`close-mask-${dpi}.png`),new Resvg(svg,{fitTo:{mode:'width',value:Math.round(14*dpi/96)}}).render().asPng());}
// Mask-only 14 px glyphs (full opacity) for the settings header buttons: drawn at Paper's exact 70%.
for(const [alias,source] of [['back','arrow-left'],['reload','arrow-counter-clockwise']]){const svg=fs.readFileSync(path.join(root,'phosphor-bold',source+'-bold.svg'),'utf8').replace('fill="currentColor"','fill="#FFFFFF"');
  fs.writeFileSync(path.join(root,`${alias}-mask.svg`),svg);
  for(const dpi of [96,120,144,168,192,240,288,384])fs.writeFileSync(path.join(root,`${alias}-mask-${dpi}.png`),new Resvg(svg,{fitTo:{mode:'width',value:Math.round(14*dpi/96)}}).render().asPng());}
// Bookmarks: the filled star is the saved state, drawn at full white like Paper (the outline keeps 50%).
{const svg=fs.readFileSync(path.join(root,'phosphor-bold','star-fill.svg'),'utf8').replace('fill="currentColor"','fill="#FFFFFF"');
  fs.writeFileSync(path.join(root,'star-fill.svg'),svg);
  for(const dpi of [96,120,144,168,192,240,288,384])fs.writeFileSync(path.join(root,`star-fill-${dpi}.png`),new Resvg(svg,{fitTo:{mode:'width',value:Math.round(16*dpi/96)}}).render().asPng());}
