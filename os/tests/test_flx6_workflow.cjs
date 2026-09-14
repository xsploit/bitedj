const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const root = path.resolve(__dirname, '../..');
let now = 1000;
const values = new Map([['[Tab]:library',1],['[Sidebar]:sidebar_visible',0],['[PiFlex]:browse_acceleration',1]]);
const calls=[];
const context={console, Date:{now:()=>now}, engine:{
  isScratching:()=>false,
  getValue:(g,k)=>values.get(g+':'+k)||0,
  setValue:(g,k,v)=>{values.set(g+':'+k,v); calls.push([g,k,v]);},
}, script:{triggerControl:(g,k)=>calls.push([g,k,1])}};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(root,'res/controllers/Pioneer-DDJ-FLX6-script.js'),'utf8'),context);
const mapping=context.PioneerDDJFLX6;
mapping.browseRotate(6,64,1); assert.equal(calls.at(-1)[2],1);
now+=20; mapping.browseRotate(6,64,1); assert.equal(calls.at(-1)[2],10);
now+=20; mapping.browseRotate(6,64,127); assert.equal(calls.at(-1)[2],-1);
now+=20; mapping.browseRotate(6,64,127); assert.equal(calls.at(-1)[2],-10);
now+=200; mapping.browseRotate(6,64,1); assert.equal(calls.at(-1)[2],1);
values.set('[Sidebar]:sidebar_visible',1);
now+=20; mapping.browseRotate(6,64,1); assert.equal(calls.at(-1)[2],1);
values.set('[Sidebar]:sidebar_visible',0);
values.set('[PiFlex]:browse_acceleration',0);
now+=20; mapping.browseRotate(6,64,1); assert.equal(calls.at(-1)[2],1);
values.set('[PiFlex]:browse_acceleration',1);
calls.length=0; mapping.waveformZoom(6,100,127,0,'[Channel1]');
assert.deepEqual(calls,[['[Channel1]','waveform_zoom_up',1],['[Channel2]','waveform_zoom_up',1]]);
for(let deck=0;deck<4;deck++){
  calls.length=0; mapping.shiftButtonDown=[false,false,false,false];
  mapping.shiftPressed(deck,0,127); values.set(`[Channel${deck+1}]:track_loaded`,1);
  for(let i=0;i<16;i++) mapping.jogSearch(deck,0x29,65,0,`[Channel${deck+1}]`);
  for(let i=0;i<16;i++) mapping.jogTurn(deck,0,63,0,`[Channel${deck+1}]`);
  assert.deepEqual(calls,[[`[Channel${deck+1}]`,'beats_translate_move',1],[`[Channel${deck+1}]`,'beats_translate_move',-1]]);
  for(let i=0;i<8;i++) mapping.jogSearch(deck,0x29,65,0,`[Channel${deck+1}]`);
  mapping.shiftPressed(deck,0,0); assert.equal(mapping.gridJogResidual[deck],0);
}
calls.length=0; mapping.shiftButtonDown=[true,true,false,false];
values.set('[Channel1]:track_loaded',0);
mapping.jogSearch(0,0x29,127,0,'[Channel1]'); assert.equal(calls.length,0);
mapping.shiftButtonDown=[false,false,false,false];
let period=0; mapping.changeBeatFxPeriodBy=v=>period+=v;
mapping.beatFxLeftPressed(4,6,127); assert.equal(period,-1);
mapping.beatFxRightPressed(4,7,127); assert.equal(period,0);
mapping.beatFxLeftPressed(4,6,0); assert.equal(period,0);
calls.length=0;
context.engine.isScratching=()=>true;
context.engine.scratchDisable=(deck,ramp)=>calls.push(['scratchDisable',deck,ramp]);
mapping.shiftPressed(0,0,127);
assert.deepEqual(calls,[['scratchDisable',1,false]]);
console.log('FLX6: acceleration, reversal, sidebar precision, linked zoom, four-deck shifted jog, residual reset, empty deck, FX preservation PASS');

// Check the shipping XML as well as JS: testing viewPressed alone missed that
// the physical button was still wired directly to a toggle control.
const xml = fs.readFileSync(path.join(root,'res/controllers/Pioneer-DDJ-FLX6.midi.xml'),'utf8');
const controls = [...xml.matchAll(/<control>([\s\S]*?)<\/control>/g)].map(m=>m[1]);
const view = controls.filter(c=>/<status>0x96<\/status>/.test(c) && /<midino>0x7A<\/midino>/.test(c));
assert.equal(view.length,1,'one physical VIEW binding');
assert.match(view[0],/<key>PioneerDDJFLX6.viewPressed<\/key>/);
assert.match(view[0],/<script-binding\s*\/>/);
for (const collapsed of [0,1]) {
  values.set('[Skin]:player_right_tools',collapsed);
  values.set('[Skin]:player_left_rail',collapsed);
  values.set('[Tab]:library',0);
  for (let press=0;press<3;press++) {
    mapping.viewPressed(6,0x7a,127);
    mapping.viewPressed(6,0x7a,0);
    assert.equal(values.get('[Tab]:library'),1,'VIEW stays in Browse');
    calls.length=0; now+=200;
    mapping.browseRotate(6,0x40,1);
    assert.deepEqual(calls,[['[Library]','MoveVertical',1]]);
    assert.equal(values.get('[Skin]:player_right_tools'),collapsed);
    assert.equal(values.get('[Skin]:player_left_rail'),collapsed);
  }
}
console.log('FLX6: shipping VIEW binding, repeated press/release, encoder after rail collapse PASS');
