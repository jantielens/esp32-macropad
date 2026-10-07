const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const families = ['Space Grotesk','Source Sans 3','Manrope','IBM Plex Mono','Barlow','Barlow Condensed','DM Sans','Bitter','Chakra Petch','Nunito Sans','IBM Plex Sans'];
const query = families.map(family=>'family='+family.replaceAll(' ','+')+':wght@400;500;600;700').join('&');
const dir = path.join(__dirname,'fonts');
fs.mkdirSync(dir,{recursive:true});
let css = execFileSync('curl',['-fLsS','https://fonts.googleapis.com/css2?'+query+'&display=swap'],{encoding:'utf8'});
const urls = [...new Set([...css.matchAll(/url\((https:[^)]+)\)/g)].map(match=>match[1]))];
for (const [index,url] of urls.entries()) {
  const file = `font-${index}${path.extname(new URL(url).pathname)}`;
  execFileSync('curl',['-fLsS',url,'-o',path.join(dir,file)]);
  css = css.replaceAll(url,'/lab/fonts/'+file);
}
fs.writeFileSync(path.join(dir,'fonts.css'),css);
console.log(`Downloaded ${urls.length} local font files.`);
let emojiCSS = execFileSync('curl',['-fLsS','https://fonts.googleapis.com/css2?family=Noto+Color+Emoji&display=swap'],{encoding:'utf8'});
const emojiURLs = [...new Set([...emojiCSS.matchAll(/url\((https:[^)]+)\)/g)].map(match=>match[1]))];
for (const [index,url] of emojiURLs.entries()) {
  const file = `emoji-${index}${path.extname(new URL(url).pathname)}`;
  execFileSync('curl',['-fLsS',url,'-o',path.join(dir,file)]);
  emojiCSS = emojiCSS.replaceAll(url,'/lab/fonts/'+file);
}
fs.writeFileSync(path.join(dir,'emoji.css'),emojiCSS);
console.log(`Downloaded ${emojiURLs.length} local color emoji font files.`);
const licenses = path.join(__dirname,'licenses');
fs.mkdirSync(licenses,{recursive:true});
for (const family of [...families,'Noto Color Emoji']) {
  const slug = family.toLowerCase().replaceAll(' ','');
  execFileSync('curl',['-fLsS',`https://raw.githubusercontent.com/google/fonts/main/ofl/${slug}/OFL.txt`,'-o',path.join(licenses,slug+'-OFL.txt')]);
}
execFileSync('curl',['-fLsS','https://cdn.jsdelivr.net/npm/lucide@0.468.0/LICENSE','-o',path.join(licenses,'lucide-LICENSE.txt')]);
execFileSync('curl',['-fLsS','https://cdn.jsdelivr.net/npm/feather-icons@4.29.2/LICENSE','-o',path.join(licenses,'feather-LICENSE.txt')]);
console.log('Downloaded font and Lucide redistribution licenses.');