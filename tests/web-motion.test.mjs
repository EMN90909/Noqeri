import test from 'node:test';
import assert from 'node:assert/strict';
import {createMotion} from '../Runtime/noqeri-web-runtime.mjs';
import {compileView,parseView} from '../Runtime/web-view-compiler.mjs';
import {buildWebProject} from '../Runtime/web-project.mjs';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

function environment(){
  const registry=new Map();
  const add=(selector,count=1)=>{
    const elements=Array.from({length:count},()=>({
      animations:[],
      animate(frames,options){
        const anim={frames,options,events:[],
          pause(){this.events.push('pause')},
          play(){this.events.push('play')},
          reverse(){this.events.push('reverse')},
          cancel(){this.events.push('cancel')}
        };
        this.animations.push(anim);return anim;
      }
    }));
    registry.set(selector,elements);return elements;
  };
  return {add,root:{querySelectorAll:selector=>registry.get(selector)??[]}};
}
const source=[
 'motion hero { target: ".hero" trigger: "timeline" fromOpacity: 0 duration: 180 }',
 'motion copy { target: ".copy" trigger: "timeline" fromY: 22 duration: 130 }',
 'timeline intro { hero copy }',
 'motion reveal { target: ".reveal" trigger: "scroll" fromScale: 90 }',
 'view { h1 class: "hero" { "Hello" } p class: "copy" { "World" } div class: "reveal" { "Reveal" } }'
].join('\n');

test('motion grammar parity in experimental Node browser compiler',()=>{
 const ast=parseView(source);
 assert.equal(ast.motions.length,3);
 assert.deepEqual(ast.timelines[0].steps,['hero','copy']);
 const js=compileView(source);
 assert.match(js,/createMotion as __createMotion/);
 assert.match(js,/__createMotion\(root,/);
 assert.match(js,/__motion.dispose\(\)/);
});

test('timeline offsets, staggering, playback controls, idempotence and cleanup',()=>{
 const e=environment(),hero=e.add('.hero',2),copy=e.add('.copy');
 const motion=createMotion(e.root,[
   {name:'hero',target:'.hero',trigger:'timeline',duration:200,stagger:50,fromOpacity:0},
   {name:'copy',target:'.copy',trigger:'timeline',duration:100,fromY:22}
 ],[{name:'intro',steps:['hero','copy']}]);
 assert.equal(hero[0].animations[0].options.delay,0);
 assert.equal(hero[1].animations[0].options.delay,50);
 assert.equal(copy[0].animations[0].options.delay,250);
 assert.equal(hero[0].animations[0].frames[0].opacity,0);
 assert.equal(motion.pause('intro'),true);
 assert.deepEqual(copy[0].animations[0].events,['pause']);
 assert.equal(motion.reverse('intro'),true);
 assert.equal(motion.restart('intro'),true);
 assert.equal(copy[0].animations[0].currentTime,0);
 assert.equal(motion.play('missing'),false);
 motion.refresh();
 assert.equal(hero[0].animations.length,1,'refresh should not replay attached nodes');
 motion.dispose();
 assert.equal(copy[0].animations[0].events.at(-1),'cancel');
});

test('timeline overlap and scroll replay maintain ordering and observer identity',()=>{
 const e=environment(),hero=e.add('.hero',2),copy=e.add('.copy');
 const sequence=createMotion(e.root,[
   {name:'hero',target:'.hero',trigger:'timeline',duration:200,stagger:50},
   {name:'copy',target:'.copy',trigger:'timeline',duration:100,offset:-100}
 ],[{name:'intro',steps:['hero','copy']}]);
 assert.equal(hero[1].animations[0].options.delay,50);
 assert.equal(copy[0].animations[0].options.delay,150);
 sequence.dispose();
 const previous=globalThis.IntersectionObserver,observers=[];
 globalThis.IntersectionObserver=class{
   constructor(callback){this.callback=callback;observers.push(this)}
   observe(element){this.element=element}
   disconnect(){this.disconnected=true}
 };
 try{
   const elements=e.add('.repeat');
   const scroll=createMotion(e.root,[{name:'reveal',target:'.repeat',trigger:'scroll',once:false}]);
   scroll.refresh();scroll.refresh();
   assert.equal(observers.length,1);
   const entry={target:elements[0]};
   observers[0].callback([{...entry,isIntersecting:true}]);
   observers[0].callback([{...entry,isIntersecting:false}]);
   observers[0].callback([{...entry,isIntersecting:true}]);
   assert.equal(elements[0].animations.length,2);
   scroll.dispose();
   assert.equal(observers[0].disconnected,true);
 }finally{globalThis.IntersectionObserver=previous}
});
test('scroll starts only on intersection, then disconnects on dispose',()=>{
 const old=globalThis.IntersectionObserver,observers=[];
 globalThis.IntersectionObserver=class{
   constructor(callback){this.callback=callback;observers.push(this)}
   observe(element){this.element=element}
   disconnect(){this.disconnected=true}
   unobserve(){this.unobserved=true}
 };
 try{
   const e=environment(),cards=e.add('.cards',2);
   const motion=createMotion(e.root,[{name:'cards',target:'.cards',trigger:'scroll',stagger:40}]);
   assert.equal(cards[0].animations.length,0);
   observers[0].callback([{isIntersecting:true,target:cards[0]}]);
   assert.equal(cards[0].animations[0].options.delay,0);
   observers[1].callback([{isIntersecting:true,target:cards[1]}]);
   assert.equal(cards[1].animations[0].options.delay,40);
   motion.dispose();
   assert.ok(observers.every(o=>o.disconnected));
 }finally{globalThis.IntersectionObserver=old}
});

test('respects reduced motion and rejects unsafe declarations',()=>{
 const previous=globalThis.matchMedia;
 globalThis.matchMedia=()=>({matches:true});
 try{
   const e=environment(),el=e.add('.thing');
   const motion=createMotion(e.root,[{name:'thing',target:'.thing',fromOpacity:0}]);
   assert.equal(el[0].animations.length,0);
   motion.dispose();
 }finally{globalThis.matchMedia=previous}
 const e=environment();
 assert.throws(()=>createMotion(e.root,[{name:'evil',target:'body'}]),/safe class\/id/);
 assert.throws(()=>createMotion(e.root,[{name:'bad',target:'.safe',duration:Infinity}]),/invalid duration/);
 assert.throws(()=>createMotion(e.root,[{name:'bad',target:'.safe',eval:1}]),/unsupported property/);
 for(const invalid of [
   'motion m { target: "body" }\nview { p { "x" } }',
   'motion m { target: ".x" duration: 30001 }\nview { p { "x" } }',
   'motion m { target: ".x" offset: -50 }\nview { p { "x" } }',
   'motion m { target: ".x" trigger: "timeline" offset: -30001 }\ntimeline t { m }\nview { p { "x" } }',
   'motion m { target: ".x" trigger: "timeline" }\nview { p { "x" } }',
   'motion m { target: ".x" }\ntimeline a { m }\nview { p { "x" } }'
 ])assert.throws(()=>compileView(invalid),/Noqeri view:/);
});

test('Node web build contains a bundled independent motion runtime',async()=>{
 const root=await mkdtemp(join(tmpdir(),'noqeri-motion-test-'));
 try{
   const sourceDir=join(root,'src'),output=join(root,'dist');
   await mkdir(sourceDir);
   await writeFile(join(sourceDir,'main.noqeri'),source);
   await buildWebProject({source:sourceDir,out:output});
   assert.match(await readFile(join(output,'main.mjs'),'utf8'),/__createMotion/);
   assert.match(await readFile(join(output,'__noqeri/runtime.mjs'),'utf8'),/createMotion/);
   assert.match(await readFile(join(output,'index.html'),'utf8'),/Hello/);
 }finally{await rm(root,{recursive:true,force:true})}
});
