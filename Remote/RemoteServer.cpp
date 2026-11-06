#include "Remote/RemoteServer.h"

#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QHostAddress>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNetworkInterface>
#include <QPainter>
#include <QPixmap>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWheelEvent>
#include <QSettings>

#include "Config.h"

static const int    kMaxRequest  = 64 * 1024;
static const int    kMaxFrameW   = 1600;
static const qint64 kClientAlive = 4000;

static const char *kPage = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<title>KaraMixer Edition</title>
<style>
html,body{margin:0;height:100%;background:#07080d;color:#7dffc0;font:13px sans-serif;overflow:hidden;touch-action:none;overscroll-behavior:none}
#s{position:absolute;left:0;top:0;touch-action:none;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none;-webkit-user-drag:none;max-width:none}
#msg{position:fixed;left:0;right:0;top:0;text-align:center;padding:6px;background:rgba(0,0,0,.75);display:none;z-index:3}
#bar{touch-action:manipulation;position:fixed;right:calc(6px + env(safe-area-inset-right));top:50%;transform:translateY(-50%);display:flex;flex-direction:column;gap:6px;z-index:2;opacity:.8}
#bar.min button.x{display:none}
button{touch-action:manipulation;-webkit-tap-highlight-color:transparent;background:#14202a;color:#7dffc0;border:1px solid #1f5a45;border-radius:8px;padding:8px 12px;font-size:14px;min-width:40px}
</style></head><body>
<div id="msg"></div>
<img id="s" alt="">
<div id="bar"><button id="tg">&#9776;</button><button class="x" id="zi">+</button><button class="x" id="zo">&minus;</button><button class="x" id="rt">&#10227;</button><button class="x" id="mx">MIX</button><button class="x" id="esc">Esc</button><button class="x" id="fs">&#9974;</button></div>
<script>
var T=new URLSearchParams(location.search).get('t')||'';
var img=document.getElementById('s'),msg=document.getElementById('msg');
var h='',fails=0,dead=false;
var bar=document.getElementById('bar');
var rotMode=0,zoom=1,px=0,py=0,ROT=false;
function say(t){if(t){msg.textContent=t;msg.style.display='block';}else{msg.style.display='none';}}
function layout(){
  var iw=img.naturalWidth,ih=img.naturalHeight;if(!iw||!ih)return;
  var vv=window.visualViewport;
  var vw=Math.min(document.documentElement.clientWidth,window.innerWidth||1e9,vv?vv.width:1e9);
  var vh=Math.min(document.documentElement.clientHeight,window.innerHeight||1e9,vv?vv.height:1e9);
  var rot=(rotMode==1)||(rotMode==0&&vh>vw&&iw>ih*1.15);
  var bw=rot?ih:iw,bh=rot?iw:ih;
  var s=Math.min(vw/bw,vh/bh)*zoom;
  var w=iw*s,hh=ih*s,W=rot?hh:w,H=rot?w:hh;
  var mx=Math.max(0,(W-vw)/2),my=Math.max(0,(H-vh)/2);
  px=Math.max(-mx,Math.min(mx,px));py=Math.max(-my,Math.min(my,py));
  img.style.width=w+'px';img.style.height=hh+'px';
  img.style.left=((vw-w)/2+px)+'px';img.style.top=((vh-hh)/2+py)+'px';
  img.style.transform=rot?'rotate(90deg)':'none';
  ROT=rot;
  var imgTop=(vh-hh)/2+py;
  if(rot){
    bar.style.flexDirection='column';bar.style.left='auto';
    bar.style.right='calc(6px + env(safe-area-inset-right))';
    bar.style.top='50%';bar.style.transform='translateY(-50%)';
  }else{
    bar.style.flexDirection='row';bar.style.right='auto';bar.style.transform='none';
    var bw=bar.offsetWidth,bhh=bar.offsetHeight;
    bar.style.left=Math.max(4,Math.min(vw-bw-4,vw/2+px-bw/2))+'px';
    var minTop=52;                                  // keep clear of the browser's own top bar
    var above=imgTop-minTop,below=vh-(imgTop+hh)-24;
    var top;
    if(above>=bhh+14)top=imgTop-bhh-14;             // above the mixer (preferred)
    else if(below>=bhh+14)top=imgTop+hh+8;          // otherwise below it
    else top=minTop;
    bar.style.top=Math.max(minTop,top)+'px';
    // safety: measure what really happened and lift the bar off the image
    var rc=bar.getBoundingClientRect();
    if(top<imgTop&&rc.bottom>imgTop-6){
      bar.style.top=Math.max(0,rc.top-(rc.bottom-(imgTop-8)))+'px';
    }
  }
}
window.addEventListener('resize',layout);
if(window.visualViewport)window.visualViewport.addEventListener('resize',layout);
window.addEventListener('orientationchange',function(){setTimeout(layout,200);});
function fetchT(url,opt,ms){
  // never wait forever on a weak Wi-Fi link
  var ac=(typeof AbortController!=='undefined')?new AbortController():null;
  var tm=setTimeout(function(){if(ac)ac.abort();},ms);
  opt=opt||{};if(ac)opt.signal=ac.signal;
  return fetch(url,opt).then(function(r){clearTimeout(tm);return r;},function(e){clearTimeout(tm);throw e;});
}
var gotFrame=true;
function loop(){
  if(dead)return;
  gotFrame=false;
  fetchT('/f?t='+encodeURIComponent(T)+'&h='+h,{cache:'no-store'},5000).then(function(r){
    if(r.status==403){dead=true;say('QRหมดอายุ/ปิดอยู่ กรุณาสแกนใหม่');return;}
    if(r.status==200){
      h=r.headers.get('X-H')||'';gotFrame=true;
      return r.blob().then(function(b){
        var u=URL.createObjectURL(b),old=img.src;
        img.onload=function(){if(old&&old.indexOf('blob:')==0)URL.revokeObjectURL(old);layout();};
        img.src=u;
      });
    }
  }).then(function(){fails=0;say('');setTimeout(loop,gotFrame?30:90);})
    .catch(function(){fails++;say('กำลังเชื่อมต่อ...');setTimeout(loop,Math.min(2000,250*fails));});
}
loop();

var pend=[],sending=false;
function pump(){
  if(sending||!pend.length)return;
  sending=true;
  var o=pend.shift();
  fetchT('/e?t='+encodeURIComponent(T),{method:'POST',body:new URLSearchParams(o)},2500)
    .catch(function(){}).then(function(){sending=false;pump();});
}
function send(o){
  if(o.type=='move'&&pend.length&&pend[pend.length-1].type=='move')pend[pend.length-1]=o;
  else pend.push(o);
  pump();
}
function pos(e){
  var r=img.getBoundingClientRect();
  var dx=e.clientX-r.left,dy=e.clientY-r.top,x,y;
  if(!ROT){x=dx/r.width;y=dy/r.height;}else{x=dy/r.height;y=1-dx/r.width;}
  return {x:Math.max(0,Math.min(1,x)),y:Math.max(0,Math.min(1,y))};
}
var pts={},n=0,down=false,last=null,cx=0,cy=0,acc=0;
function centroid(){
  var k=Object.keys(pts),sx=0,sy=0;
  for(var i=0;i<k.length;i++){sx+=pts[k[i]].clientX;sy+=pts[k[i]].clientY;}
  return {x:sx/k.length,y:sy/k.length};
}
img.addEventListener('pointerdown',function(e){
  e.preventDefault();img.setPointerCapture(e.pointerId);
  pts[e.pointerId]=e;n=Object.keys(pts).length;
  if(n==1){var p=pos(e);last=p;down=true;send({type:'down',x:p.x,y:p.y});}
  else if(n==2){
    if(down){send({type:'cancel',x:last.x,y:last.y});down=false;}
    var c=centroid();cx=c.x;cy=c.y;acc=0;
  }
});
img.addEventListener('pointermove',function(e){
  if(!pts[e.pointerId])return;
  e.preventDefault();pts[e.pointerId]=e;
  if(n==1&&down){var p=pos(e);last=p;send({type:'move',x:p.x,y:p.y});}
  else if(n==2){
    var c=centroid(),dx=c.x-cx,dy=c.y-cy;cx=c.x;cy=c.y;
    if(zoom>1.01){px+=dx;py+=dy;layout();}
    else{
      acc+=ROT?-dx:dy;
      if(Math.abs(acc)>10){var p2=pos(c);send({type:'wheel',x:p2.x,y:p2.y,dy:Math.round(acc)});acc=0;}
    }
  }
});
function end(e){
  if(!pts[e.pointerId])return;
  e.preventDefault();
  var wasOne=(n==1);
  delete pts[e.pointerId];n=Object.keys(pts).length;
  if(wasOne&&down){var p=pos(e);send({type:'up',x:p.x,y:p.y});down=false;}
}
img.addEventListener('pointerup',end);
img.addEventListener('pointercancel',end);
img.addEventListener('contextmenu',function(e){e.preventDefault();});
document.addEventListener('touchmove',function(e){e.preventDefault();},{passive:false});
try{if(localStorage.getItem('bm_min')=='1')bar.className='min';}catch(e){}
function btn(id,fn){
  var b=document.getElementById(id);
  b.addEventListener('pointerdown',function(e){
    e.preventDefault();e.stopPropagation();
    b.style.background='#1f5a45';setTimeout(function(){b.style.background='';},140);
    fn();
  });
  b.addEventListener('click',function(e){e.preventDefault();e.stopPropagation();});
}
btn('tg',function(){
  bar.className=(bar.className=='min')?'':'min';
  try{localStorage.setItem('bm_min',bar.className=='min'?'1':'0');}catch(e){}
  layout();
});
btn('esc',function(){send({type:'key',key:'esc'});});
btn('mx',function(){location.href='/m?t='+encodeURIComponent(T);});   // the phone mixer page
btn('zi',function(){zoom=Math.min(4,zoom*1.35);layout();});
btn('zo',function(){zoom=Math.max(1,zoom/1.35);if(zoom<1.02){zoom=1;px=0;py=0;}layout();});
btn('rt',function(){rotMode=(rotMode+1)%3;px=0;py=0;layout();});
var de=document.documentElement;
if(!(de.requestFullscreen||de.webkitRequestFullscreen))document.getElementById('fs').style.display='none';   // e.g. iPhone Safari
btn('fs',function(){
  var inFs=document.fullscreenElement||document.webkitFullscreenElement;
  if(inFs){if(document.exitFullscreen)document.exitFullscreen();else if(document.webkitExitFullscreen)document.webkitExitFullscreen();}
  else{if(de.requestFullscreen)de.requestFullscreen();else if(de.webkitRequestFullscreen)de.webkitRequestFullscreen();}
  setTimeout(layout,300);
});
document.addEventListener('fullscreenchange',function(){setTimeout(layout,200);});
</script></body></html>
)HTML";

// The phone MIXER page (/m): one fader / M / S per strip and the master, drawn by the phone itself (no picture of the
// window), so nothing has to be shown, clicked or opened on the computer. Two literals: MSVC allows 16 KB for each.
static const char *kMixerPage = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-title" content="KaraMixer">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="theme-color" content="#0b0d12">
<link rel="apple-touch-icon" href="/icon.png">
<link rel="icon" type="image/png" href="/icon.png?s=192">
<title>KaraMixer Edition</title>
<style>
:root{--fh:200px;--tb:42px;--fl:max(4px,env(safe-area-inset-left));--fr:max(4px,env(safe-area-inset-right));--ft:max(4px,env(safe-area-inset-top));--fb:max(4px,env(safe-area-inset-bottom))}
html,body{margin:0;height:100%;background:#000;color:#dfe6f0;font:13px sans-serif;overflow:hidden;overscroll-behavior:none;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none}
html{touch-action:manipulation;-webkit-text-size-adjust:100%;text-size-adjust:100%}*{-webkit-user-select:none;user-select:none;-webkit-touch-callout:none;-webkit-tap-highlight-color:transparent}
#frm{position:fixed;left:var(--fl);right:var(--fr);top:var(--ft);bottom:var(--fb);border:1px solid #3f4552;border-radius:10px;background:#0b0d12;box-shadow:0 4px 18px rgba(0,0,0,.7);pointer-events:none}
#top{position:fixed;left:calc(var(--fl) + 1px);right:calc(var(--fr) + 1px);top:calc(var(--ft) + 1px);min-height:42px;display:flex;flex-wrap:nowrap;align-items:center;gap:4px 5px;padding:4px 6px 4px 8px;background:linear-gradient(#23262e,#171a20);border-bottom:1px solid #3a3f4b;border-radius:9px 9px 0 0;z-index:3;box-sizing:border-box}
#brand{display:flex;align-items:center;gap:8px;flex:none;min-width:0}
#lg{width:32px;height:32px;flex:none;display:block}
#bt{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:14px;font-weight:bold;letter-spacing:.6px;color:#fff}
#dot{width:10px;height:10px;border-radius:50%;background:#e5484d;flex:none}
#dot.ok{background:#3ddc84}
#tw{display:flex;gap:5px;flex:1 1 0;min-width:0}
#tabs{display:flex;gap:5px;flex:0 1 auto;min-width:0;overflow-x:auto;scrollbar-width:none}
#tabs::-webkit-scrollbar{display:none}
#rt{display:flex;gap:5px;flex:none}
#top.cmp #bt .ed{display:none}#top.cmp .tab,#top.cmp .tl{padding:7px 7px}#top.cmp .tab.pre{width:122px}
#top.cmp2 .tab.pre{width:96px}#top.cmp2 .tab.pre small{display:none}#top.cmp2 #rt{gap:4px}
#top.cmp3 #bt{display:none}#top.cmp3 #tabs{min-width:40px}
.tab.pre{width:138px;box-sizing:border-box;overflow:hidden;text-overflow:ellipsis;text-align:center;border-color:#a33a3a;background:#2a1517;color:#fff}
.tab.pre small{font-size:11px;color:#ff9b9b;margin-right:4px}
#pp{position:fixed;left:0;top:0;right:0;bottom:0;z-index:5;display:none;background:rgba(0,0,0,.55)}
#pp.open{display:block}
#pl{position:absolute;left:8px;right:8px;max-width:640px;margin:0 auto;top:calc(var(--tb) + 6px);max-height:calc(100% - var(--tb) - 20px);overflow-y:auto;background:#141821;border:1px solid #2b3a52;border-radius:12px;padding:10px;box-sizing:border-box;box-shadow:0 8px 24px rgba(0,0,0,.6)}
#pl .ph{font-weight:bold;color:#c9d4e6;margin:0 2px 8px}
#pg{display:grid;grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:6px}
#pg a{padding:10px 6px;border:1px solid #2b3a52;border-radius:8px;background:#182131;color:#c9d4e6;font-size:14px;text-align:center;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#pg a.on{background:#d92b2b;border-color:#ff7a7a;color:#fff;font-weight:bold}
@media (max-width:359px){#lg{width:28px;height:28px}#bt{font-size:12.5px;letter-spacing:0}}
.tab,.tl{flex:none;padding:7px 10px;border:1px solid #2b3a52;border-radius:8px;background:#182131;color:#c9d4e6;font-size:13px;text-decoration:none;white-space:nowrap}
.tab.on{background:#1f7ae0;border-color:#8cc4ff;color:#fff}
#row{position:fixed;left:calc(var(--fl) + 1px);right:calc(var(--fr) + 93px);top:var(--tb);bottom:calc(var(--fb) + 1px);border-bottom-left-radius:9px;display:flex;overflow-x:auto;overflow-y:hidden;-webkit-overflow-scrolling:touch}
#mst{position:fixed;right:calc(var(--fr) + 1px);top:var(--tb);bottom:calc(var(--fb) + 1px);width:92px;background:#1b1116;border-left:1px solid #4a2530;border-bottom-right-radius:9px;display:flex;flex-direction:column;align-items:center;padding:6px 0;box-sizing:border-box}
.st{flex:none;width:66px;padding:6px 3px;box-sizing:border-box;display:flex;flex-direction:column;align-items:center;border-right:1px solid #1c2534}
.nm{height:22px;line-height:22px;max-width:100%;overflow:hidden;white-space:nowrap;text-overflow:ellipsis;font-weight:bold;font-size:12px}
.g0 .nm{color:#6ec1ff}.g1 .nm{color:#ffb35c}.g2 .nm{color:#c79bff}.g3 .nm{color:#7dffc0}.g4 .nm{color:#ff8fb0}#mst .nm{color:#ff6b6b}
.fw{display:flex;gap:6px}
.fd{position:relative;width:38px;height:var(--fh);touch-action:none}
.fd:before{content:"";position:absolute;left:50%;top:0;bottom:0;width:4px;margin-left:-2px;background:#222c3d;border-radius:2px}
.th{position:absolute;left:0;width:100%;height:24px;border-radius:6px;background:linear-gradient(#e8edf5,#9aa6b8);box-shadow:0 1px 3px #000;pointer-events:none}
.th:after{content:"";position:absolute;left:6px;right:6px;top:11px;height:2px;background:#333}
.mt{position:relative;width:8px;height:var(--fh);background:linear-gradient(to top,#2fd07a 0%,#2fd07a 62%,#f5c542 82%,#ff4d4d 100%);border-radius:2px;overflow:hidden}
.mt b{position:absolute;left:0;right:0;top:0;height:100%;background:#0f141d}
.mt i{position:absolute;left:0;right:0;top:100%;height:2px;background:#fff;display:none}
.db{height:18px;line-height:18px;font-size:11px;color:#9fb0c8}
button{touch-action:manipulation;-webkit-tap-highlight-color:transparent;border:1px solid #2b3a52;border-radius:8px;background:#182131;color:#c9d4e6;font-size:13px;font-weight:bold;width:46px;height:30px;margin-top:3px;padding:0}
#mst button.bm{width:52px}
button.bm.on{background:#d92b2b;border-color:#ff7a7a;color:#fff}
button.bs.on{background:#e8a60c;border-color:#ffd36b;color:#111}
button.bd{width:80px;font-size:12px}
#msg{position:fixed;left:0;right:0;top:var(--tb);text-align:center;padding:6px;background:rgba(0,0,0,.8);display:none;z-index:4;color:#7dffc0}
</style></head><body>
<div id="frm"></div>
<div id="top"><div id="brand"><img id="lg" alt=""><b id="bt">KaraMixer<span class="ed"> Edition</span></b><span id="dot"></span></div><div id="tw"><div id="tabs"></div><a class="tab pre" id="pb">พรีเซ็ท &#9662;</a></div><div id="rt"><a class="tl" id="sl">&#9664;</a><a class="tl" id="sr">&#9654;</a><a class="tl" id="fsb">&#9974;</a><a class="tl" id="scr">ภาพจอ</a></div></div>
<div id="msg"></div>
<div id="pp"><div id="pl"><div class="ph">เลือกพรีเซ็ท</div><div id="pg"></div></div></div>
<div id="row"></div>
<div id="mst"><div class="nm">MASTER</div><div class="fw"><div class="fd" id="mf"><div class="th"></div></div><div class="mt"><b id="mu"></b><i id="mp"></i></div></div><div class="db" id="md">0.0 dB</div><button class="bm" id="mb">MUTE</button><button class="bs bd" id="dr">โซโล่กลอง</button></div>
)HTML" R"HTML(<script>
var T=new URLSearchParams(location.search).get('t')||'';
var NAMES=['ที่ใช้อยู่','เครื่องดนตรี','กลอง','BUS / อินพุต','ทั้งหมด'];
var tab=0;try{tab=parseInt(localStorage.getItem('km_tab')||'0',10)||0;}catch(e){}
if(tab<0||tab>=NAMES.length)tab=0;
var S={},ORDER=[],M={v:50,m:0,t:0,d:0,dt:0},FH=200,dead=false,h='';
function $(id){return document.getElementById(id);}
var row=$('row'),msg=$('msg'),dot=$('dot');
$('scr').href='/?t='+encodeURIComponent(T);
$('lg').src='/l?t='+encodeURIComponent(T);
// "Add to Home Screen": the manifest gives the icon a start page with this access code, so the icon opens the mixer directly
(function(){var ml=document.createElement('link');ml.rel='manifest';ml.href='/manifest.json?t='+encodeURIComponent(T);document.head.appendChild(ml);})();
function say(t){if(t){msg.textContent=t;msg.style.display='block';}else{msg.style.display='none';}}
function clamp(v,a,b){return Math.max(a,Math.min(b,v));}
function dbTxt(l){if(l<=0)return '-inf dB';var d=l>=100?12:(l>=50?(l-50)*12/50:-60*(50-l)/50);return (d>0.05?'+':'')+d.toFixed(1)+' dB';}
function fetchT(url,opt,ms){
  var ac=(typeof AbortController!=='undefined')?new AbortController():null;
  var tm=setTimeout(function(){if(ac)ac.abort();},ms);
  opt=opt||{};if(ac)opt.signal=ac.signal;
  return fetch(url,opt).then(function(r){clearTimeout(tm);return r;},function(e){clearTimeout(tm);throw e;});
}
var q=[],busy=false;
function post(o){var l=q[q.length-1];if(l&&l.k==o.k&&l.i==o.i)q[q.length-1]=o;else q.push(o);pump();}
function pump(){
  if(busy||!q.length)return;
  busy=true;var o=q.shift();
  fetchT('/c?t='+encodeURIComponent(T),{method:'POST',body:new URLSearchParams(o)},2500).catch(function(){}).then(function(){busy=false;pump();});
}
function place(fd,v){fd.firstChild.style.top=((1-v/100)*(FH-24))+'px';}
// Meters: the phone asks for the levels ~30 times a second (/u); here every meter glides to its latest level (fast up) and
// shows a peak-hold line like the ones on the computer's screen (a new top is held 0.5 s, then falls to the level).
var MT=[];
function newMeter(b,i){var m={b:b,i:i,cur:0,tgt:0,pk:0,pkT:0,dc:-1,dp:-1};MT.push(m);return m;}
function frame(now){
  for(var k=0;k<MT.length;k++){
    var m=MT[k];
    m.cur+=(m.tgt-m.cur)*0.6;
    if(Math.abs(m.tgt-m.cur)<0.4)m.cur=m.tgt;
    if(m.cur>=m.pk){m.pk=m.cur;m.pkT=now;}else if(now-m.pkT>500){m.pk=m.cur;m.pkT=now;}
    var c=Math.round(clamp(m.cur/127,0,1)*200),p=Math.round(clamp(m.pk/127,0,1)*200);
    if(c!=m.dc){m.dc=c;m.b.style.height=(100-c/2)+'%';}
    if(p!=m.dp){m.dp=p;m.i.style.top=(100-p/2)+'%';m.i.style.display=p>2?'block':'none';}
  }
  requestAnimationFrame(frame);
}
function bindFader(fd,get,set){
  var drag=null,last=0;
  fd.addEventListener('pointerdown',function(e){
    e.preventDefault();
    var now=Date.now();
    if(now-last<320){last=0;drag=null;set(50);return;}
    last=now;try{fd.setPointerCapture(e.pointerId);}catch(x){}drag={y:e.clientY,v:get()};
  });
  fd.addEventListener('pointermove',function(e){
    if(!drag)return;e.preventDefault();
    set(clamp(Math.round(drag.v+(drag.y-e.clientY)/Math.max(40,FH-24)*100),0,100));
  });
  function end(){if(drag){drag=null;set(get());}}
  fd.addEventListener('pointerup',end);fd.addEventListener('pointercancel',end);
}
function draw(o){
  place(o.fd,o.v);o.db.textContent=dbTxt(o.v);
  o.bm.className='bm'+(o.m?' on':'');o.bs.className='bs'+(o.s?' on':'');
}
function mk(s){
  var el=document.createElement('div');el.className='st g'+s.g;
  el.innerHTML='<div class="nm"></div><div class="fw"><div class="fd"><div class="th"></div></div><div class="mt"><b></b><i></i></div></div><div class="db"></div><button class="bm">M</button><button class="bs">S</button>';
  var fw=el.children[1];
  var o={id:s.i,el:el,fd:fw.children[0],mt:newMeter(fw.children[1].children[0],fw.children[1].children[1]),db:el.children[2],bm:el.children[3],bs:el.children[4],g:s.g,l:s.l,v:s.v,m:s.m,s:s.s,t:0};
  el.children[0].textContent=s.n;
  bindFader(o.fd,function(){return o.v;},function(v){o.v=v;o.t=Date.now();draw(o);post({k:'vol',i:o.id,v:v});});
  o.bm.addEventListener('click',function(){o.m=o.m?0:1;o.t=Date.now();draw(o);post({k:'mute',i:o.id,v:o.m});});
  o.bs.addEventListener('click',function(){o.s=o.s?0:1;o.t=Date.now();draw(o);post({k:'solo',i:o.id,v:o.s});});
  return o;
}
var mf=$('mf'),mu=$('mu'),md=$('md'),mb=$('mb'),dr=$('dr'),MM=newMeter(mu,$('mp'));
function drawM(){place(mf,M.v);md.textContent=dbTxt(M.v);mb.className='bm'+(M.m?' on':'');dr.className='bs bd'+(M.d?' on':'');}
bindFader(mf,function(){return M.v;},function(v){M.v=v;M.m=0;M.t=Date.now();drawM();post({k:'mvol',i:-1,v:v});});
mb.addEventListener('click',function(){M.m=M.m?0:1;M.t=Date.now();drawM();post({k:'mmute',i:-1,v:M.m});});
dr.addEventListener('click',function(){M.d=M.d?0:1;M.dt=Date.now();drawM();post({k:'dsolo',i:-1,v:M.d});});
function vis(o){
  switch(tab){
    case 0:return o.g!=3&&o.l;
    case 1:return o.g==0||o.g==2;
    case 2:return o.g==1;
    case 3:return o.g==3||o.g==4;
    default:return true;
  }
}
function filt(){for(var k=0;k<ORDER.length;k++){var o=ORDER[k];o.el.style.display=vis(o)?'flex':'none';}}
function mkTabs(){
  var t=$('tabs');
  NAMES.forEach(function(n,i){
    var b=document.createElement('a');b.className='tab'+(i==tab?' on':'');b.textContent=n;
    b.addEventListener('click',function(){
      tab=i;try{localStorage.setItem('km_tab',String(i));}catch(e){}
      for(var k=0;k<t.children.length;k++)t.children[k].className='tab'+(k==tab?' on':'');
      filt();row.scrollLeft=0;
    });
    t.appendChild(b);
  });
}
mkTabs();
// Preset button (after the tabs): the soundfont preset of the page tabs on the computer, chosen from a list that opens
// under the bar - eleven tabs do not fit on a phone
var P={c:0,n:[],t:0},pp=$('pp'),pg=$('pg'),pb=$('pb');
function drawP(){
  pb.innerHTML='';var sm=document.createElement('small');sm.textContent='พรีเซ็ท';pb.appendChild(sm);
  pb.appendChild(document.createTextNode((P.n.length?(P.n[P.c]||String(P.c)):'')+' ▾'));
  for(var k=0;k<pg.children.length;k++)pg.children[k].className=(k==P.c?'on':'');
}
function buildP(){
  pg.innerHTML='';
  P.n.forEach(function(n,i){
    var a=document.createElement('a');a.textContent=n;
    a.addEventListener('click',function(e){e.stopPropagation();P.c=i;P.t=Date.now();drawP();post({k:'preset',i:-1,v:i});pp.className='';});
    pg.appendChild(a);
  });
  drawP();
}
pb.addEventListener('click',function(){if(P.n.length)pp.className='open';});
pp.addEventListener('click',function(e){if(e.target==pp)pp.className='';});
drawP();
$('sl').addEventListener('click',function(){row.scrollLeft-=row.clientWidth*0.8;});
$('sr').addEventListener('click',function(){row.scrollLeft+=row.clientWidth*0.8;});
function apply(d){
  var now=Date.now(),chg=false,i,s,o;
  for(i=0;i<d.strips.length;i++){
    s=d.strips[i];o=S[s.i];
    if(!o){o=mk(s);S[s.i]=o;ORDER.push(o);row.appendChild(o.el);draw(o);chg=true;}
    if(o.g!=s.g||o.l!=s.l)chg=true;
    o.g=s.g;o.l=s.l;
    if(now-o.t>900){o.v=s.v;o.m=s.m;o.s=s.s;draw(o);}
  }
  // the strips stay in the computer's order (it changes when BUS is hidden: YouTube / Audio move); the meters use it too
  var ids=d.strips.map(function(x){return x.i;}).join(',');
  if(ids!=ORDER.map(function(x){return x.id;}).join(',')){
    ORDER=d.strips.map(function(x){return S[x.i];});
    for(i=0;i<ORDER.length;i++)row.appendChild(ORDER[i].el);
    chg=true;
  }
  if(now-M.t>900){M.v=d.master.v;M.m=d.master.m;drawM();}
  if(now-M.dt>900&&M.d!=(d.master.d?1:0)){M.d=d.master.d?1:0;drawM();}
  if(d.pn&&d.pn.join('|')!=P.n.join('|')){P.n=d.pn;buildP();}
  if(typeof d.pre=='number'&&now-P.t>900&&d.pre!=P.c){P.c=d.pre;drawP();}
  if(chg)filt();
}
function setConn(ok){dot.className=ok?'ok':'';say(ok?'':'กำลังเชื่อมต่อ...');}
function poll(){
  if(dead)return;
  fetchT('/s?t='+encodeURIComponent(T)+'&h='+h,{cache:'no-store'},4000).then(function(r){
    if(r.status==403){dead=true;dot.className='';say('QR หมดอายุ/ปิดอยู่ กรุณาสแกนใหม่');return null;}
    if(r.status==200){h=r.headers.get('X-H')||'';return r.json();}
    return null;
  }).then(function(d){
    if(dead)return;
    if(d)apply(d);
    setConn(true);setTimeout(poll,document.hidden?1500:250);
  }).catch(function(){if(!dead){setConn(false);setTimeout(poll,800);}});
}
// the meters, ~30 times a second (a tiny list of numbers, see SynthMixerDialog::remoteMetersCsv)
function pollU(){
  if(dead)return;
  var t0=Date.now();
  fetchT('/u?t='+encodeURIComponent(T),{cache:'no-store'},1500).then(function(r){return r.status==200?r.text():null;}).then(function(txt){
    if(txt){
      var a=txt.split(',');
      if(a.length==ORDER.length+1){for(var k=0;k<ORDER.length;k++)ORDER[k].mt.tgt=+a[k];MM.tgt=+a[ORDER.length];}
    }
    setTimeout(pollU,document.hidden?1000:Math.max(0,33-(Date.now()-t0)));
  }).catch(function(){setTimeout(pollU,500);});
}
function layout(){
  // the top bar is always one line (a second line would make the faders shorter): when the tabs do not all fit, the name
  // drops "Edition" and the buttons get narrower; if they still do not fit, the tabs scroll sideways
  var top=$('top'),tabs=$('tabs');
  top.className='';
  // step by step until the tabs fit: no "Edition" + narrower buttons, then a short preset button, then the logo only
  var steps=['cmp','cmp cmp2','cmp cmp2 cmp3'];
  for(var i=0;i<steps.length&&tabs.scrollWidth>tabs.clientWidth+1;i++)top.className=steps[i];
  var tb=Math.round(top.getBoundingClientRect().bottom)||42;   // where the bar ends (it sits inside the frame)
  document.documentElement.style.setProperty('--tb',tb+'px');
  // the strip area (minus the iPhone's home bar in full screen) less name, dB and the M / S buttons
  FH=Math.max(110,($('row').clientHeight||((window.innerHeight||600)-tb))-120);
  document.documentElement.style.setProperty('--fh',FH+'px');
  for(var k=0;k<ORDER.length;k++)draw(ORDER[k]);
  drawM();
}
window.addEventListener('resize',layout);window.addEventListener('load',layout);
window.addEventListener('orientationchange',function(){setTimeout(layout,250);});
// A mixer, not a document: the page keeps its size. iPhone Safari ignores user-scalable=no, so a quick second tap (the
// fader reset) zoomed the page in and a held tap opened the text look-up bar: both are stopped here.
// Full-screen button for Android Chrome (it hides the browser's bars). Not shown where it cannot work (iPhone: use "Add to
// Home Screen") or when the page already runs from a home-screen icon.
(function(){
  var de=document.documentElement,b=$('fsb');
  var req=de.requestFullscreen||de.webkitRequestFullscreen;
  var home=window.navigator.standalone===true||(window.matchMedia&&matchMedia('(display-mode: fullscreen), (display-mode: standalone)').matches);
  if(!req||home){b.style.display='none';return;}
  b.addEventListener('click',function(){
    var r;
    if(document.fullscreenElement||document.webkitFullscreenElement)r=(document.exitFullscreen||document.webkitExitFullscreen).call(document);
    else r=req.call(de);
    if(r&&r.catch)r.catch(function(){});
  });
  document.addEventListener('fullscreenchange',function(){setTimeout(layout,200);});
  document.addEventListener('webkitfullscreenchange',function(){setTimeout(layout,200);});
})();
function noZoom(e){e.preventDefault();}
document.addEventListener('gesturestart',noZoom,{passive:false});document.addEventListener('gesturechange',noZoom,{passive:false});
document.addEventListener('dblclick',noZoom,{passive:false});document.addEventListener('contextmenu',noZoom);document.addEventListener('selectstart',noZoom);
document.addEventListener('touchmove',function(e){if(e.touches.length>1)e.preventDefault();},{passive:false});
var lastTap=0;document.addEventListener('touchend',function(e){var n=Date.now(),t=e.target;
  if(n-lastTap<400&&!(t.closest&&t.closest('button,a')))e.preventDefault();lastTap=n;},{passive:false});
layout();poll();pollU();requestAnimationFrame(frame);
</script></body></html>
)HTML";

// ---------------------------------------------------------------------------

RemoteServer::RemoteServer(QWidget *baseWindow, QWidget *ignoreWindow, QObject *parent)
    : QObject(parent), mainWin(baseWindow), ignoreWin(ignoreWindow)
{
    clock.start();
    lastGrab.start();
    connect(&server, &QTcpServer::newConnection, this, &RemoteServer::onNewConnection);

    // close connections that were idle for a while
    idleTimer.setInterval(5000);
    connect(&idleTimer, &QTimer::timeout, this, [this]() {
        const QList<QTcpSocket *> socks = findChildren<QTcpSocket *>();
        for (QTcpSocket *s : socks)
            if (clock.elapsed() - s->property("last").toLongLong() > 20000)
                s->abort();

        // a phone that went away is noticed here (the phone-control window and the main window follow the count)
        const int n = clientCount();
        if (n != lastClientCount) {
            lastClientCount = n;
            emit clientsChanged();
        }
    });
}

RemoteServer::~RemoteServer()
{
    stop();
}

QStringList RemoteServer::localAddresses()
{
    QStringList prefer, other;

    const QList<QNetworkInterface> ifs = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &ni : ifs)
    {
        if (!(ni.flags() & QNetworkInterface::IsUp) ||
            !(ni.flags() & QNetworkInterface::IsRunning) ||
            (ni.flags() & QNetworkInterface::IsLoopBack))
            continue;

        const QList<QNetworkAddressEntry> entries = ni.addressEntries();
        for (const QNetworkAddressEntry &e : entries)
        {
            QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            QString s = a.toString();
            if (s.startsWith("169.254."))       // link-local, useless
                continue;

            quint32 v = a.toIPv4Address();
            bool priv = (v >> 24) == 10 ||
                        (v >> 20) == 0xAC1 ||           // 172.16/12
                        (v >> 16) == 0xC0A8;            // 192.168/16
            if (priv) prefer << s; else other << s;
        }
    }
    return prefer + other;
}

// random access code (the QR code / link carries it)
QString RemoteServer::makeToken()
{
    static const char alphabet[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString t;
    for (int i = 0; i < 16; i++)
        t += QChar(alphabet[QRandomGenerator::global()->bounded((int)sizeof(alphabet) - 1)]);
    return t;
}

// a saved code that was edited by hand must not break the link: letters and digits only, 16 of them
bool RemoteServer::validToken(const QString &t)
{
    if (t.size() != 16)
        return false;
    for (int i = 0; i < t.size(); i++)
        if (t.at(i).unicode() > 127 || !t.at(i).isLetterOrNumber())
            return false;
    return true;
}

bool RemoteServer::autoStartEnabled()
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    return st.value(QStringLiteral("RemoteAutoStart"), false).toBool();
}

void RemoteServer::setAutoStartEnabled(bool on)
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    st.setValue(QStringLiteral("RemoteAutoStart"), on);
}

QString RemoteServer::savedIp()
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    return st.value(QStringLiteral("RemoteIp")).toString();
}

void RemoteServer::setMixerApi(std::function<QByteArray()> state,
                               std::function<void(const QMap<QString, QString> &)> change,
                               std::function<QByteArray()> meters)
{
    mixState = std::move(state);
    mixChange = std::move(change);
    mixMeters = std::move(meters);
}

bool RemoteServer::start(const QString &ip)
{
    stop();

    ipText = ip;

    // The access code and the port are kept between runs (KaraMixer.conf): the link in the phone stays the same, also
    // after the program was closed and opened again.
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    token = st.value(QStringLiteral("RemoteToken")).toString();
    if (!validToken(token))
    {
        token = makeToken();
        st.setValue(QStringLiteral("RemoteToken"), token);
    }

    QList<quint16> ports;
    const int saved = st.value(QStringLiteral("RemotePort"), 0).toInt();
    if (saved >= 8765 && saved < 8775)
        ports << quint16(saved);
    for (quint16 p = 8765; p < 8775; p++)
        if (!ports.contains(p))
            ports << p;

    for (quint16 p : ports)
    {
        if (server.listen(QHostAddress::AnyIPv4, p))
        {
            st.setValue(QStringLiteral("RemotePort"), int(p));
            st.setValue(QStringLiteral("RemoteIp"), ip);
            idleTimer.start();
            return true;
        }
    }

    token.clear();
    return false;
}

// start at program start: the IP used last time if the computer still has it, else the first one found
bool RemoteServer::startAuto()
{
    if (server.isListening())
        return true;

    const QStringList addrs = localAddresses();
    if (addrs.isEmpty())
        return false;                   // no network yet (Wi-Fi still connecting): the main window tries again

    const QString saved = savedIp();
    return start(addrs.contains(saved) ? saved : addrs.first());
}

// A new access code; the old link / QR gets "Forbidden" from the next request on.
void RemoteServer::newToken()
{
    const QString t = makeToken();
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    st.setValue(QStringLiteral("RemoteToken"), t);

    if (server.isListening())
    {
        token = t;
        clients.clear();
        lastClientCount = 0;
        emit clientsChanged();
    }
}

void RemoteServer::stop()
{
    idleTimer.stop();
    if (server.isListening())
        server.close();

    // drop all open connections
    const QList<QTcpSocket *> socks = findChildren<QTcpSocket *>();
    for (QTcpSocket *s : socks)
    {
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }

    token.clear();
    clients.clear();
    lastClientCount = 0;
    dragWindow = nullptr;
    pressWidget = nullptr;
    emit clientsChanged();
}

QString RemoteServer::url() const
{
    if (!server.isListening())
        return QString();
    return QString("http://%1:%2/m?t=%3").arg(ipText).arg(server.serverPort()).arg(token);
}

int RemoteServer::clientCount() const
{
    qint64 now = clock.elapsed();
    int n = 0;
    for (auto it = clients.constBegin(); it != clients.constEnd(); ++it)
        if (now - it.value() < kClientAlive)
            n++;
    return n;
}

void RemoteServer::touchClient(const QString &ip)
{
    bool isNew = !clients.contains(ip) || (clock.elapsed() - clients.value(ip) >= kClientAlive);
    clients[ip] = clock.elapsed();
    if (isNew)
    {
        lastClientCount = clientCount();
        emit clientsChanged();
    }
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

void RemoteServer::onNewConnection()
{
    while (server.hasPendingConnections())
    {
        QTcpSocket *s = server.nextPendingConnection();
        s->setParent(this);
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);   // no Nagle delay
        s->setProperty("last", clock.elapsed());

        connect(s, &QTcpSocket::readyRead, this, [this, s]() { onReadyRead(s); });
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    }
}

void RemoteServer::onReadyRead(QTcpSocket *s)
{
    s->setProperty("last", clock.elapsed());

    QByteArray buf = s->property("buf").toByteArray();
    buf += s->readAll();

    // keep-alive: one socket may carry many requests
    while (!buf.isEmpty())
    {
        if (buf.size() > kMaxRequest * 2)
        {
            s->abort();
            return;
        }

        int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0)
            break;                                  // wait for the rest

        QByteArray head = buf.left(headEnd);
        QList<QByteArray> lines = head.split('\n');
        QList<QByteArray> reqLine = lines.value(0).trimmed().split(' ');
        if (reqLine.size() < 2)
        {
            s->abort();
            return;
        }

        int contentLength = 0;
        for (int i = 1; i < lines.size(); i++)
        {
            QByteArray l = lines[i].trimmed();
            if (l.toLower().startsWith("content-length:"))
                contentLength = l.mid(15).trimmed().toInt();
        }

        if (contentLength < 0 || contentLength > kMaxRequest)
        {
            s->abort();
            return;
        }

        int total = headEnd + 4 + contentLength;
        if (buf.size() < total)
            break;                                  // body not complete yet

        QByteArray body = buf.mid(headEnd + 4, contentLength);
        buf.remove(0, total);

        QByteArray target = reqLine[1];
        QByteArray path = target;
        QByteArray query;
        int q = target.indexOf('?');
        if (q >= 0)
        {
            path = target.left(q);
            query = target.mid(q + 1);
        }

        handle(s, reqLine[0], QString::fromLatin1(path), query, body);
    }

    s->setProperty("buf", buf);
}

void RemoteServer::reply(QTcpSocket *s, int code, const QByteArray &contentType,
                         const QByteArray &body, const QByteArray &extraHeaders)
{
    QByteArray status;
    switch (code) {
    case 200: status = "OK"; break;
    case 204: status = "No Content"; break;
    case 403: status = "Forbidden"; break;
    default:  status = "Not Found"; code = 404; break;
    }

    QByteArray out = "HTTP/1.1 " + QByteArray::number(code) + " " + status + "\r\n";
    out += "Content-Type: " + contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "Connection: keep-alive\r\nKeep-Alive: timeout=15\r\n";
    out += "Access-Control-Expose-Headers: X-H\r\n";
    out += extraHeaders;
    out += "\r\n";
    out += body;

    s->write(out);
}

void RemoteServer::handle(QTcpSocket *s, const QByteArray &method, const QString &path,
                          const QByteArray &query, const QByteArray &body)
{
    QUrlQuery uq{QString::fromLatin1(query)};

    // The home-screen icon of the phone mixer page: the phone asks for it without the access code (it is only the logo).
    // The logo on the dark background of the page, 180 px for the iPhone, 192 / 512 for Android.
    if (path == "/icon.png")
    {
        int size = uq.queryItemValue("s").toInt();
        if (size != 192 && size != 512)
            size = 180;
        static QMap<int, QByteArray> icons;
        if (!icons.contains(size))
        {
            QImage img(size, size, QImage::Format_ARGB32);
            img.fill(QColor(0x14, 0x18, 0x21));
            const QImage logo(QStringLiteral(":/Icons/App/karamixer.png"));
            if (!logo.isNull())
            {
                const QImage l = logo.scaled(size * 82 / 100, size * 82 / 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QPainter p(&img);
                p.drawImage((size - l.width()) / 2, (size - l.height()) / 2, l);
            }
            QByteArray png;
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
            icons.insert(size, png);
        }
        reply(s, 200, "image/png", icons.value(size));
        return;
    }

    if (token.isEmpty() || uq.queryItemValue("t") != token)
    {
        reply(s, 403, "text/plain; charset=utf-8", "Forbidden");
        return;
    }

    QString ip = s->peerAddress().toString();
    if (ip.startsWith("::ffff:"))
        ip = ip.mid(7);

    if (path == "/")
    {
        touchClient(ip);
        reply(s, 200, "text/html; charset=utf-8", QByteArray(kPage));
        return;
    }

    if (path == "/f")
    {
        touchClient(ip);

        if (!grabFrame())
        {
            reply(s, 204, "text/plain", QByteArray());
            return;
        }

        if (uq.queryItemValue("h") == lastHash)
        {
            reply(s, 204, "text/plain", QByteArray());
            return;
        }

        reply(s, 200, "image/jpeg", lastJpeg, "X-H: " + lastHash.toLatin1() + "\r\n");
        return;
    }

    if (path == "/manifest.json")           // "Add to Home Screen": name, icon, and a start page with the access code
    {
        const QByteArray js = "{\"name\":\"KaraMixer Edition\",\"short_name\":\"KaraMixer\",\"start_url\":\"/m?t="
                              + QUrl::toPercentEncoding(token) + "\",\"scope\":\"/\",\"display\":\"fullscreen\","
                              "\"background_color\":\"#0b0d12\",\"theme_color\":\"#0b0d12\",\"icons\":["
                              "{\"src\":\"/icon.png?s=192\",\"sizes\":\"192x192\",\"type\":\"image/png\"},"
                              "{\"src\":\"/icon.png?s=512\",\"sizes\":\"512x512\",\"type\":\"image/png\"}]}";
        reply(s, 200, "application/manifest+json", js);
        return;
    }

    if (path == "/l")                       // the logo of the phone mixer page's top bar: the one in the mixer's title bar
    {
        static QByteArray logo;
        if (logo.isEmpty())
        {
            QFile f(QStringLiteral(":/Icons/App/karamixer-title.png"));
            if (f.open(QIODevice::ReadOnly))
                logo = f.readAll();
        }

        if (logo.isEmpty())
        {
            reply(s, 404, "text/plain", "Not found");
            return;
        }

        reply(s, 200, "image/png", logo);
        return;
    }

    if (path == "/m")                       // the phone mixer page
    {
        touchClient(ip);
        reply(s, 200, "text/html; charset=utf-8", QByteArray(kMixerPage));
        return;
    }

    if (path == "/s")                       // its state, as JSON (204 when it is the same as the phone already has)
    {
        touchClient(ip);

        if (!mixState)
        {
            reply(s, 404, "text/plain", "Not found");
            return;
        }

        const QByteArray js = mixState();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(js, QCryptographicHash::Md5).toHex().left(12));
        if (uq.queryItemValue("h") == hash)
        {
            reply(s, 204, "text/plain", QByteArray());
            return;
        }

        reply(s, 200, "application/json; charset=utf-8", js, "X-H: " + hash.toLatin1() + "\r\n");
        return;
    }

    if (path == "/u")                       // the meters only, tiny and fast: the phone mixer page asks ~30 times a second
    {
        touchClient(ip);

        if (!mixMeters)
        {
            reply(s, 404, "text/plain", "Not found");
            return;
        }

        reply(s, 200, "text/plain; charset=utf-8", mixMeters());
        return;
    }

    if (path == "/c" && method == "POST")   // one change from the phone mixer page (fader / mute / solo)
    {
        touchClient(ip);

        QMap<QString, QString> p;
        QUrlQuery bq{QString::fromUtf8(body)};
        const QList<QPair<QString, QString> > items = bq.queryItems(QUrl::FullyDecoded);
        for (const QPair<QString, QString> &it : items)
            p.insert(it.first, it.second);

        if (mixChange)
            mixChange(p);
        reply(s, 200, "text/plain", "ok");
        return;
    }

    if (path == "/e" && method == "POST")
    {
        touchClient(ip);

        QMap<QString, QString> p;
        QUrlQuery bq{QString::fromUtf8(body)};
        const QList<QPair<QString, QString> > items = bq.queryItems(QUrl::FullyDecoded);
        for (const QPair<QString, QString> &it : items)
            p.insert(it.first, it.second);

        // answer first: an injected click may open a modal dialog (e.g. the exit
        // question) whose nested event loop would otherwise block this reply
        reply(s, 200, "text/plain", "ok");
        QTimer::singleShot(0, this, [this, p]() { inject(p); });
        return;
    }

    reply(s, 404, "text/plain", "Not found");
}

// ---------------------------------------------------------------------------
// screen mirroring
// ---------------------------------------------------------------------------

QWidget *RemoteServer::targetWindow() const
{
    if (QWidget *w = QApplication::activePopupWidget())
        return w;
    if (QWidget *w = QApplication::activeModalWidget())
        if (!w->property("buaiRemoteSkip").toBool())
            return w;

    QWidget *a = QApplication::activeWindow();
    if (a && a != ignoreWin && a->isVisible() && !a->isMinimized() &&
        !a->property("buaiRemoteSkip").toBool())
        return a;

    return mainWin;
}

bool RemoteServer::grabFrame()
{
    // share one capture between several phones / fast polling
    if (!lastJpeg.isEmpty() && lastGrab.elapsed() < 40)
        return true;

    // an open popup menu is always shown, even while a press is still in progress
    QWidget *w = QApplication::activePopupWidget();
    if (!w)
        w = dragWindow ? dragWindow.data() : targetWindow();
    if (!w || w->width() < 2 || w->height() < 2)
        return false;

    QImage img = w->grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (img.isNull())
        return false;

    if (img.width() > kMaxFrameW)
        img = img.scaledToWidth(kMaxFrameW, Qt::SmoothTransformation);

    QCryptographicHash md5(QCryptographicHash::Md5);
    md5.addData(reinterpret_cast<const char *>(img.constBits()), int(img.sizeInBytes()));
    // the source window identity / size is part of the hash
    md5.addData(QByteArray::number(reinterpret_cast<quintptr>(w)));
    md5.addData(QByteArray::number(img.width()) + "x" + QByteArray::number(img.height()));
    QString hash = QString::fromLatin1(md5.result().toHex().left(16));

    lastGrab.restart();

    if (hash == lastHash && !lastJpeg.isEmpty())
        return true;

    QByteArray jpg;
    QBuffer buf(&jpg);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "JPEG", 70);

    lastJpeg = jpg;
    lastHash = hash;
    return !lastJpeg.isEmpty();
}

// ---------------------------------------------------------------------------
// input injection
// ---------------------------------------------------------------------------

void RemoteServer::inject(const QMap<QString, QString> &p)
{
    const QString type = p.value("type");

    if (type == "key")
    {
        if (p.value("key") == "esc")
        {
            QWidget *w = QApplication::activePopupWidget();
            if (!w) w = QApplication::focusWidget();
            if (!w) w = targetWindow();
            if (!w)
                return;
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(w, &press);
            QApplication::sendEvent(w, &release);
            lastJpeg.clear();
        }
        return;
    }

    double nx = qBound(0.0, p.value("x").toDouble(), 1.0);
    double ny = qBound(0.0, p.value("y").toDouble(), 1.0);

    QWidget *win = nullptr;
    if (type == "down")
        win = targetWindow();
    else if (dragWindow)
        win = dragWindow.data();
    else
        win = targetWindow();

    if (!win)
        return;

    QPoint wp(qBound(0, qRound(nx * win->width()),  win->width()  - 1),
              qBound(0, qRound(ny * win->height()), win->height() - 1));

    if (type == "down")
    {
        QWidget *child = win->childAt(wp);
        if (!child)
            child = win;

        dragWindow = win;
        pressWidget = child;

        QPoint lp = child->mapFrom(win, wp);
        QPoint gp = win->mapToGlobal(wp);
        QMouseEvent ev(QEvent::MouseButtonPress, QPointF(lp), QPointF(gp),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(child, &ev);
    }
    else if (type == "move")
    {
        if (!pressWidget)
            return;
        QPoint lp = pressWidget->mapFrom(win, wp);
        QPoint gp = win->mapToGlobal(wp);
        QMouseEvent ev(QEvent::MouseMove, QPointF(lp), QPointF(gp),
                       Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(pressWidget.data(), &ev);
    }
    else if (type == "up" || type == "cancel")
    {
        QWidget *pw = pressWidget.data();
        QWidget *pwin = win;
        if (pw)
        {
            // cancel: release far outside the widget, so buttons do not "click"
            QPoint lp = (type == "cancel") ? QPoint(-5000, -5000) : pw->mapFrom(win, wp);
            QPoint gp = win->mapToGlobal(wp);
            QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(lp), QPointF(gp),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(pw, &ev);        // may run a nested event loop
        }
        // a new press may have started meanwhile (inside that nested loop)
        if (pressWidget.data() == pw)
        {
            pressWidget = nullptr;
            dragWindow = nullptr;
        }
        Q_UNUSED(pwin);
    }
    else if (type == "wheel")
    {
        QWidget *child = win->childAt(wp);
        if (!child)
            child = win;
        QPoint lp = child->mapFrom(win, wp);
        QPoint gp = win->mapToGlobal(wp);
        int dy = p.value("dy").toInt() * 3;
        QWheelEvent ev(QPointF(lp), QPointF(gp), QPoint(0, 0), QPoint(0, dy),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(child, &ev);
    }

    // the screen changes because of the input: force a fresh capture
    lastJpeg.clear();
}
