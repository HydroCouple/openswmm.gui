// Design-artifact checks only. These do not execute the proposed engine coupling.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { chromium } = require('playwright');
const dir = __dirname;
const checks = [], browserErrors = [];
const close = (a,b) => assert.ok(Math.abs(a-b)<1e-12, `${a} != ${b}`);
function pass(name) { checks.push({ name, status:'passed' }); console.log('PASS: '+name); }
async function main() {
  const browser = await chromium.launchPersistentContext(path.join(dir,'browser-profile'),{headless:true,executablePath:process.env.R4_REVIEW_BROWSER||'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'});
  try {
    const page=await browser.newPage({viewport:{width:1100,height:900}});
    page.on('pageerror',e=>browserErrors.push(e.message));
    await page.goto(pathToFileURL(path.join(dir,'remap-review.html')).href);
    assert.equal(await page.locator('#apply').isDisabled(),true);
    await page.locator('#uniform').check();
    assert.equal(await page.locator('#apply').isDisabled(),false);
    pass('Explicit uniform assumption required before preview Apply');
    const data=()=>page.evaluate(()=>window.r4Preview());
    let d=await data();
    close(d.demand,.84); close(d.accepted,.5);
    assert.deepEqual(d.rows.map(r=>r.area),[500,240,100]);
    close(d.rows[0].accepted,.5*.5/.84);
    close(d.rows[1].accepted,.24*.5/.84);
    close(d.rows[2].accepted,.1*.5/.84);
    pass('Half-inside partition excludes LID from non-LID area; capped claims share one receiver');
    await page.locator('#inspect').click();
    assert.equal(await page.locator('#budget').isVisible(),true);
    pass('Cell inspection selects receiving-budget view');
    for(let v=0;v<=100;v++) {
      await page.locator('#headroom').evaluate((e,v)=>{e.value=String(v/100);e.dispatchEvent(new Event('input',{bubbles:true}));},v);
      d=await data();
      close(d.accepted,Math.min(d.headroom,d.demand));
      close(d.rows.reduce((s,r)=>s+r.unaccepted,0)+d.accepted,d.demand);
      for(const r of d.rows){assert.ok(r.accepted>=0&&r.after>=-1e-12);close(r.after+r.accepted,r.water);}
    }
    pass('101 headroom values conserve candidate and donor volumes, with bounded acceptance');
    const setScenario=async(name)=>{await page.locator('#scenario').selectOption(name);await page.locator('#tab-ownership').click();await page.locator('#uniform').check();};
    await setScenario('saturated');d=await data();
    close(d.accepted,0);assert.equal(await page.locator('#headroom').isDisabled(),true);
    assert.ok(d.rows.every(r=>r.after===r.water));
    pass('Closed saturation accepts zero and keeps water with all originating donors');
    await setScenario('shared');
    await page.locator('#scope').selectOption('selected');d=await data();
    assert.deepEqual(d.rows.map(r=>r.area),[300,240,100,200]);
    close(d.demand,.84);close(d.accepted,.5);
    assert.ok((await page.locator('#scope-note').textContent()).includes('S2'));
    // Independent reorder of this displayed fixture verifies the proposed formula.
    const reversed=[...d.rows].reverse();const sum=reversed.reduce((s,r)=>s+r.candidate,0);
    for(const r of reversed)close(r.candidate*Math.min(1,d.headroom/sum),r.accepted);
    pass('Selected scope still includes competing S2; illustrative allocation is order invariant');
    await setScenario('limited');d=await data();close(d.rows[2].candidate,.02);close(d.demand,.76);
    assert.ok(d.rows[2].accepted<.02);close(d.rows[2].after+d.rows[2].accepted,.02);
    pass('Water-limited LID claims only its available water');
    await setScenario('dry');d=await data();close(d.rows[2].candidate,0);close(d.rows[2].accepted,0);
    pass('Dry LID receives no award');
    await setScenario('lumped');d=await data();assert.equal(d.rows.length,1);assert.equal(d.rows[0].id,'Mesh · cell 27');
    close(d.rows[0].area,500);close(d.accepted,.5);
    pass('Lumped groundwater excludes spatial subcatchment/LID claims and suppresses duplicate mesh weather');
    for(const kind of ['missing','mismatch','overlap']){
      await setScenario(kind);d=await data();assert.ok(d.blocked);
      assert.equal(await page.locator('#apply').isDisabled(),true);
      assert.equal(await page.locator('#area-diagram').isVisible(),false);
      assert.equal(await page.locator('#budget-body').isVisible(),false);
    }
    pass('Missing geometry, model/polygon mismatch and overbooked areas block Apply and hide unsupported quantities');
    await setScenario('half');
    for(const owner of ['legacy','mesh']){
      await page.locator('#owner').selectOption(owner);assert.equal(await page.locator('#apply').isDisabled(),true);
    }
    pass('Unresolved legacy ownership and unqualified mesh conversion cannot activate new coupling');
    await page.locator('#owner').selectOption('subcatch');
    await page.locator('#apply').click();
    d=await data();assert.deepEqual(d.committed,{owner:'subcatch',uniform:true});
    await page.locator('#undo').click();d=await data();assert.deepEqual(d.committed,{owner:'legacy',uniform:false});
    await page.locator('#redo').click();d=await data();assert.deepEqual(d.committed,{owner:'subcatch',uniform:true});
    await page.locator('#uniform').uncheck();await page.locator('#cancel').click();d=await data();assert.equal(d.uniform,true);
    pass('One preview transaction supports Undo/Redo and Cancel restores unapplied choices');
    await page.locator('#tab-results').click();
    assert.equal(await page.locator('#result-rows').getByText('Unavailable',{exact:true}).count(),5);
    await page.locator('#result-file').selectOption('recorded');
    assert.equal(await page.locator('#result-rows').getByText('Recorded example',{exact:true}).count(),4);
    await setScenario('saturated');
    const texts=await page.locator('#result-rows .numeric').allTextContents();
    assert.equal(texts[0],'0.000');assert.equal(texts[1],'0.000');assert.equal(texts[3],'0.000');assert.equal(texts[4],'—');
    pass('Missing output remains Unavailable; recorded saturation has actual zero values');
    fs.mkdirSync(path.join(dir,'screenshots'),{recursive:true});
    await setScenario('half');await page.locator('#tab-ownership').click();
    await page.screenshot({path:path.join(dir,'screenshots','ownership-desktop.png'),fullPage:true});
    await page.locator('#tab-budget').click();
    await page.screenshot({path:path.join(dir,'screenshots','budget-desktop.png'),fullPage:true});
    await setScenario('overlap');await page.locator('#tab-ownership').click();
    await page.screenshot({path:path.join(dir,'screenshots','overlap-desktop.png'),fullPage:true});
    const layouts=[];
    for(const theme of ['light','dark']) for(const width of [320,736,1100]){
      await page.emulateMedia({colorScheme:theme});await page.setViewportSize({width,height:900});
      await setScenario('half');
      for(const tab of ['ownership','budget','results']){
        await page.locator('#tab-'+tab).click();
        const size=await page.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth}));
        assert.ok(size.scroll<=size.width,`${theme} ${width} ${tab} overflow: ${size.scroll}`);
        const buttons=await page.locator('.footer button').all();
        const boxes=[];for(const button of buttons)boxes.push(await button.boundingBox());
        for(let i=0;i<boxes.length;i++)for(let j=i+1;j<boxes.length;j++){
          const a=boxes[i],b=boxes[j];
          assert.ok(a.x+a.width<=b.x+.1||b.x+b.width<=a.x+.1||a.y+a.height<=b.y+.1||b.y+b.height<=a.y+.1,'Footer buttons overlap');
        }
        layouts.push({theme,width,tab,page_overflow:false});
      }
      await page.locator('#tab-ownership').click();
      if(width===320)await page.screenshot({path:path.join(dir,'screenshots','ownership-'+theme+'-320.png'),fullPage:true});
      if(width===1100&&theme==='dark')await page.screenshot({path:path.join(dir,'screenshots','ownership-dark-desktop.png'),fullPage:true});
    }
    pass('Three panels fit at 320/736/1100 px in light and dark themes; footer controls do not overlap');
    await page.setViewportSize({width:736,height:900});await page.emulateMedia({colorScheme:'light'});
    await page.locator('#tab-ownership').focus();await page.keyboard.press('Tab');
    assert.equal(await page.evaluate(()=>document.activeElement.id),'tab-budget');
    await page.keyboard.press('Enter');assert.equal(await page.locator('#budget').isVisible(),true);
    pass('Native keyboard navigation activates the receiving-budget tab');
    assert.deepEqual(browserErrors,[]);pass('No browser JavaScript errors');
    fs.writeFileSync(path.join(dir,'mockup_checks.json'),JSON.stringify({scope:'Design artifact only; no runtime physics qualification',checks,layouts,browser_errors:browserErrors},null,2)+'\n');
    console.log(JSON.stringify({passed:checks.length,layout_cases:layouts.length,browser_errors:browserErrors.length}));
  } finally { await browser.close(); }
}
main().catch(e=>{console.error(e);process.exitCode=1;});
