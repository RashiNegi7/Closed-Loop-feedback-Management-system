(function(){
'use strict';
var token=sessionStorage.getItem('tok')||'',me=null,tab='';
var $=function(s){return document.querySelector(s)};
function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]})}
function toast(m){var t=$('#toast');t.textContent=m;t.className='show';setTimeout(function(){t.className=''},2600)}
function api(method,path,body){
  var h={'Content-Type':'application/json'};if(token)h.Authorization='Bearer '+token;
  return fetch(path,{method:method,headers:h,body:body===undefined?undefined:JSON.stringify(body)}).then(function(r){
    return r.json().catch(function(){return{}}).then(function(d){
      if(r.status===401&&token){token='';sessionStorage.removeItem('tok');me=null;render();}
      if(!r.ok)throw new Error(d.error||('Error '+r.status));return d;});});
}
function act(p,msg){return p.then(function(d){if(msg)toast(msg);return d}).catch(function(e){toast(e.message)})}
function when(t){return new Date(t*1000).toLocaleString()}
function tag(t){return '<span class="tag '+esc(t)+'">'+esc(t)+'</span>'}
function val(id){var e=document.getElementById(id);return e?e.value:''}

function loginView(){
  api('GET','/api/classes').catch(function(){return[]}).then(function(cls){
  var opts=(cls||[]).map(function(c){return '<option>'+esc(c.name)+'</option>'}).join('');
  $('#main').innerHTML='<div class="card"><h2>Log in</h2><label>Username</label><input id="lu"><label>Password</label><input id="lp" type="password"><button class="p" id="lgo">Log in</button></div>'+
  '<div class="card"><h2>New student? Register</h2><label>Username</label><input id="ru"><label>Password</label><input id="rp" type="password"><label>Full name</label><input id="rn"><label>Student ID</label><input id="ri"><label>Class</label><select id="rc">'+opts+'</select><button class="p" id="rgo">Register</button></div>';
  $('#lgo').onclick=function(){act(api('POST','/api/login',{username:val('lu'),password:val('lp')}).then(function(d){token=d.token;sessionStorage.setItem('tok',token);start()}))};
  $('#rgo').onclick=function(){act(api('POST','/api/register',{username:val('ru'),password:val('rp'),name:val('rn'),studentId:val('ri'),className:val('rc')}).then(function(){toast('Registered. You can log in now.')}))};
  });
}
function tabsFor(){
  if(me.role==='Student')return['Report','My reports','Notifications'];
  if(me.role==='Moderator')return['Issues','Analytics','Moderation','Admin','Notifications'];
  return['Issues','Analytics','Notifications'];
}
function render(){
  if(!token||!me){$('#who').innerHTML='';$('#nav').innerHTML='';return loginView();}
  $('#who').innerHTML=esc(me.name)+' ('+esc(me.role)+') <button class="s" id="out">Log out</button>';
  $('#out').onclick=function(){api('POST','/api/logout',{}).catch(function(){});token='';me=null;sessionStorage.removeItem('tok');render()};
  var t=tabsFor();if(t.indexOf(tab)<0)tab=t[0];
  $('#nav').innerHTML=t.map(function(x){return '<button class="'+(x===tab?'on':'')+'" data-t="'+x+'">'+x+(x==='Notifications'&&me.unread?' ('+me.unread+')':'')+'</button>'}).join('');
  Array.prototype.forEach.call(document.querySelectorAll('#nav button'),function(b){b.onclick=function(){tab=b.getAttribute('data-t');render()}});
  ({'Report':reportView,'My reports':myView,'Issues':issuesView,'Analytics':analyticsView,'Moderation':modView,'Admin':adminView,'Notifications':notifView})[tab]();
}
function start(){return api('GET','/api/me').then(function(u){me=u;render()}).catch(function(){token='';render()})}

function reportView(){
  $('#main').innerHTML='<div class="card"><h2>Report a problem</h2><label>Category</label><select id="cat"><option>Teaching</option><option>Facilities</option><option>Portal</option><option>Other</option></select><label>Describe the problem</label><textarea id="msg" rows="5"></textarea><button class="p" id="go">Submit</button></div>';
  $('#go').onclick=function(){act(api('POST','/api/reports',{category:val('cat'),message:val('msg')}).then(function(d){$('#msg').value='';toast(d.note||'Report submitted. Reference #'+(d.reference||''))}),null)};
}
function myView(){
  api('GET','/api/my/reports').then(function(list){
    $('#main').innerHTML=list.length?list.map(function(r){
      var h='<div class="card"><div class="row">'+tag(r.category)+(r.status?tag(r.status):'')+(r.priority?tag(r.priority):'')+'<span class="mut">'+when(r.at)+'</span></div><p>'+esc(r.message)+'</p>';
      if(r.note)h+='<p class="mut">'+esc(r.note)+'</p>';
      if(r.studentsAffected)h+='<p class="mut">Students reporting this: '+r.studentsAffected+'</p>';
      if(r.actionTaken)h+='<p><b>Action taken:</b> '+esc(r.actionTaken)+'</p>';
      if(r.closeNote)h+='<p class="mut">'+esc(r.closeNote)+'</p>';
      if(r.needsMyConfirmation)h+='<p><b>Is the problem solved?</b></p><textarea id="cm'+r.issueId+'" placeholder="Comment (required if not solved)" rows="2"></textarea><button class="s" data-y="'+r.issueId+'">Yes, solved</button><button class="s" data-n="'+r.issueId+'">No, still a problem</button>';
      return h+'</div>'}).join(''):'<div class="card">No reports yet.</div>';
    Array.prototype.forEach.call(document.querySelectorAll('[data-y]'),function(b){b.onclick=function(){act(api('POST','/api/issues/'+b.getAttribute('data-y')+'/confirm',{solved:true}).then(myView),'Thanks!')}});
    Array.prototype.forEach.call(document.querySelectorAll('[data-n]'),function(b){var id=b.getAttribute('data-n');b.onclick=function(){act(api('POST','/api/issues/'+id+'/confirm',{solved:false,comment:val('cm'+id)}).then(myView),'Sent back to the team')}});
  }).catch(function(e){toast(e.message)});
}
function issuesView(){
  api('GET','/api/issues?include=all').then(function(list){
    $('#main').innerHTML=(list.length?list.map(function(i){
      return '<div class="card" data-i="'+i.id+'"><div class="row">'+tag(i.priority)+tag(i.status)+tag(i.category)+'<span class="mut">score '+i.score+' · '+i.ageDays+'d old</span></div><b>'+esc(i.title)+'</b>'+(i.impact?'<div class="mut">'+esc(JSON.stringify(i.impact))+'</div>':'')+'</div>'}).join(''):'<div class="card">No issues.</div>')+'<div id="detail"></div>';
    Array.prototype.forEach.call(document.querySelectorAll('[data-i]'),function(c){c.onclick=function(){detail(c.getAttribute('data-i'))}});
  }).catch(function(e){toast(e.message)});
}
function detail(id){
  api('GET','/api/issues/'+id).then(function(i){
    var a=i.actions||{},t=i.ticketDetail,h='<div class="card"><h2>'+esc(i.title)+'</h2><div class="row">'+tag(i.priority)+tag(i.status)+'</div>';
    (i.descriptions||[]).forEach(function(d){h+='<p>'+esc(d.text)+' <span class="mut">('+esc(d.className)+')</span></p>'});
    (i.followUps||[]).forEach(function(d){h+='<p class="mut">Follow-up: '+esc(d.text)+'</p>'});
    h+='<p class="mut">Score breakdown: '+esc(JSON.stringify(i.priorityBreakdown))+'</p>';
    if(t&&t.notes)t.notes.forEach(function(n){h+='<p class="mut">Note by '+esc(n.by)+': '+esc(n.text)+'</p>'});
    (i.history||[]).forEach(function(x){h+='<div class="mut">'+when(x.at)+' – '+esc(x.text)+'</div>'});
    if(a.review)h+='<button class="s" data-a="review">Mark under review</button>';
    if(a.openTicket)h+='<button class="s" data-a="ticket">Open ticket</button>';
    if(a.claim)h+='<button class="s" data-a="claim">Take ticket</button>';
    if(a.note)h+='<br><input id="note" placeholder="Add a note"><button class="s" data-a="note">Add note</button>';
    if(a.complete)h+='<br><input id="done" placeholder="What was done?"><button class="s" data-a="done">Work done</button>';
    $('#detail').innerHTML=h+'</div>';
    var tid=t&&t.id,go=function(p,m){act(api('POST',p,go.b).then(function(){detail(id)}),m)};
    Array.prototype.forEach.call(document.querySelectorAll('[data-a]'),function(b){b.onclick=function(){
      var k=b.getAttribute('data-a');go.b={};
      if(k==='review')go('/api/issues/'+id+'/review','Updated');
      if(k==='ticket')go('/api/issues/'+id+'/ticket','Ticket opened');
      if(k==='claim')go('/api/tickets/'+tid+'/claim','Assigned to you');
      if(k==='note'){go.b={text:val('note')};go('/api/tickets/'+tid+'/note','Note added')}
      if(k==='done'){go.b={actionTaken:val('done')};go('/api/tickets/'+tid+'/done','Students asked to confirm')}}});
    $('#detail').scrollIntoView();
  }).catch(function(e){toast(e.message)});
}
function analyticsView(){
  api('GET','/api/analytics').then(function(a){
    var max=Math.max.apply(null,a.reportsPerDay.map(function(r){return r.reports}).concat([1]));
    $('#main').innerHTML='<div class="card"><h2>Overview</h2>Total issues '+a.totalIssues+' · open '+a.open+' · resolved '+a.resolved+' · closed (no response) '+a.closedNoResponse+' · open tickets '+a.openTickets+' · avg resolution '+a.avgResolutionDays+' days · confirmation rate '+a.confirmationRate+'</div>'+
    '<div class="card"><h2>By class</h2>'+a.byClass.map(function(c){return '<div>'+esc(c.className)+': '+c.studentsAffected+'/'+c.enrolled+' ('+c.percent+'%)<div class="bar" style="width:'+Math.min(100,c.percent)+'%"></div></div>'}).join('')+'</div>'+
    '<div class="card"><h2>Reports per day (14 days)</h2>'+a.reportsPerDay.map(function(r){return '<div class="mut">'+r.daysAgo+'d ago · '+r.reports+'<div class="bar" style="width:'+(r.reports/max*100)+'%"></div></div>'}).join('')+'</div>';
  }).catch(function(e){toast(e.message)});
}
function modView(){
  api('GET','/api/moderation').then(function(q){
    $('#main').innerHTML=q.length?q.map(function(r){return '<div class="card"><div class="row">'+tag(r.category)+'<span class="mut">'+esc(r.student)+' · '+esc(r.className)+'</span></div><p>'+esc(r.message)+'</p><p class="mut">Flag reason: '+esc(r.reason)+'</p><button class="s" data-m="1" data-id="'+r.id+'">Approve</button><button class="s" data-m="0" data-id="'+r.id+'">Reject</button></div>'}).join(''):'<div class="card">Nothing waiting for moderation.</div>';
    Array.prototype.forEach.call(document.querySelectorAll('[data-m]'),function(b){b.onclick=function(){act(api('POST','/api/moderation/'+b.getAttribute('data-id'),{approve:b.getAttribute('data-m')==='1'}).then(modView),'Done')}});
  }).catch(function(e){toast(e.message)});
}
function adminView(){
  $('#main').innerHTML='<div class="card"><h2>Advance days (demo clock)</h2><input id="ad" type="number" value="7"><button class="p" id="adg">Advance</button></div>'+
  '<div class="card"><h2>Class enrollment</h2><label>Class name</label><input id="en"><label>Students enrolled</label><input id="ec" type="number"><button class="p" id="eg">Save</button></div>'+
  '<div class="card"><h2>Create staff/faculty account</h2><label>Username</label><input id="au"><label>Password</label><input id="ap" type="password"><label>Name</label><input id="an"><label>Role</label><select id="ar"><option>Faculty</option><option>Staff</option><option>Moderator</option></select><button class="p" id="ag">Create</button></div>';
  $('#adg').onclick=function(){act(api('POST','/api/admin/advance-days',{days:parseInt(val('ad'),10)||0}),'Clock advanced')};
  $('#eg').onclick=function(){act(api('POST','/api/admin/enrollment',{className:val('en'),count:parseInt(val('ec'),10)||0}),'Saved')};
  $('#ag').onclick=function(){act(api('POST','/api/admin/accounts',{username:val('au'),password:val('ap'),name:val('an'),role:val('ar')}),'Account created')};
}
function notifView(){
  api('GET','/api/notifications').then(function(n){
    $('#main').innerHTML=n.items.length?n.items.map(function(x){return '<div class="card">'+esc(x.text||x.message||JSON.stringify(x))+' <span class="mut">'+(x.at?when(x.at):'')+'</span></div>'}).join(''):'<div class="card">No notifications.</div>';
    if(n.unread)api('POST','/api/notifications/read',{}).then(function(){me.unread=0;var b=document.querySelector('#nav button.on');if(b)b.textContent='Notifications'});
  }).catch(function(e){toast(e.message)});
}
if(token)start();else render();
})();
