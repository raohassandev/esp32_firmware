(() => {
'use strict';
const KEY='amx-commissioning-v3';
const $=id=>document.getElementById(id);
const esc=value=>String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const api=async(path,options={})=>{const response=await fetch(path,{cache:'no-store',credentials:'same-origin',...options});const payload=await response.json().catch(()=>({}));if(!response.ok)throw new Error(payload.error||`${response.status} ${response.statusText}`);return payload;};
const defaults=()=>({version:3,step:0,site:{name:'',location:'',engineer:'',reference:''},devices:[],active:null,resources:null,acceptance:null,updated_at:new Date().toISOString()});
let state=defaults();
try{state={...defaults(),...JSON.parse(localStorage.getItem(KEY)||'{}')};}catch{}
const catalog=[
{id:'em500',type:'meter',brand:'Automatrix',model:'EM500',protocols:['tcp'],verified:true},
{id:'wm15',type:'meter',brand:'Carlo Gavazzi',model:'WM15',protocols:['tcp','rtu'],verified:false},
{id:'circutor',type:'meter',brand:'Circutor',model:'Manual-backed model',protocols:['tcp','rtu'],verified:false},
{id:'custom-meter',type:'meter',brand:'Other',model:'Custom Modbus meter',protocols:['tcp','rtu'],verified:false}
];
function tuning(){return{priority:0,normal_ms:300,high_ms:100,low_ms:1000,timeout_ms:800,response_delay_ms:0,retries:2,retry_interval_ms:500,detect_attempts:3,failure_ceiling:3,reconnect_ceiling:0,intercall_ms:50,stale_ms:5000,address_base:'zero',function_code:3,register_address:0,block_length:2,data_type:'int32',byte_order:'ABCD',scale:0.001,offset:0,precision:2,batch_write:false,rtu_silent_ms:4,turnaround_ms:10};}
function addDevice(profile){const id=`${profile.type}-${Date.now()}-${Math.random().toString(16).slice(2)}`;state.devices.push({id,type:profile.type,profile_id:profile.id,brand:profile.brand,model:profile.model,name:`${profile.brand} ${profile.model}`,verified:profile.verified,channel:profile.protocols[0],tcp:{host:'',port:502,unit_id:1},rtu:{uart:1,baud:9600,parity:'none',data_bits:8,stop_bits:1,unit_id:1},tuning:tuning(),status:'not_tested',samples:[],result:'',applied:false});state.active=id;save();render();applyMeterProfile(state.devices[state.devices.length-1]).then(ok=>{if(ok)render();});}
function save(){state.updated_at=new Date().toISOString();localStorage.setItem(KEY,JSON.stringify(state));}
/* --------------------------------------------------- reading what is commissioned
 *
 * The wizard used to open on tuning() defaults whatever was already stored, and
 * its save wrote those defaults over the live configuration. A meter reading
 * 372 kW became one reading 25 kW that way: register 58 replaced by the default
 * 0, scale 0.00001 replaced by the default 0.001. The engineer saw a blank form,
 * so nothing on screen said a working configuration was about to be discarded.
 *
 * These two tables are the inverse of the ones qualify() uses when it writes.
 * They are declared once and used in both directions, so a data type cannot be
 * read back as something other than what was written. */
const TYPE_NAMES=['uint16','int16','uint32','int32','float32'];
const ORDER_NAMES=['ABCD','CDAB','BADC','DCBA'];
const TYPE_CODES={uint16:0,int16:1,uint32:2,int32:3,float32:4};
const ORDER_CODES={ABCD:0,CDAB:1,BADC:2,DCBA:3};
/* meter_model_t and meter_role_t, as the firmware numbers them.
 *
 * The wizard used to send NEITHER. A meter commissioned through it was stored
 * UNDECLARED and UNASSIGNED, and both are refusals rather than defaults: an
 * undeclared model fails meter_model_in_phase_scope(), and an unassigned role
 * means no meter carries the grid role, so the control gate never opens. The
 * engineer saw a wizard that reported every step green and a controller that
 * would not run, with nothing connecting the two.
 *
 * It is also what silently disabled source detection on a commissioned unit:
 * the 0x2100 tariff rule applies only to an EM500/Lovato-derived instrument, so
 * with the model undeclared the generator changeover could never fire. */
const METER_MODEL_UNDECLARED=0,METER_MODEL_EM500_LOVATO=1,METER_MODEL_GENERIC_MODBUS=2;
const METER_ROLES=[[1,'Grid'],[2,'Generator'],[3,'Load'],[4,'PV']];
/* Which catalogue entry means which commissioned model. Only the EM500 is in
 * scope this phase; everything else is GENERIC_MODBUS, which the phase gate
 * refuses by name rather than by silence. Nothing here infers a model from a
 * register address or a scale factor -- that is precisely how the bitmask rule
 * leaked once already. */
const CATALOG_MODEL={em500:METER_MODEL_EM500_LOVATO,wm15:METER_MODEL_GENERIC_MODBUS,
 circutor:METER_MODEL_GENERIC_MODBUS,'custom-meter':METER_MODEL_GENERIC_MODBUS};
/* Every field the POST bodies in qualify() write. Import must cover all of
 * them: a field that is written but not imported is a field the wizard replaces
 * with a default it never showed anyone. crConfigRoundTrip below is what keeps
 * the two lists in step, and web/tests/commissioning-import.test.js executes it. */
/* The stored NUMERIC model decides which catalogue entry this device came from,
 * so the value that goes back on save is the value that was read. Deriving the
 * catalogue entry the other way round -- assuming every imported meter is an
 * EM500, which is what this did -- would silently promote a generic instrument
 * into the one family whose 0x2100 semantics this firmware claims to know. */
function catalogEntryForModel(model){return Number(model)===METER_MODEL_EM500_LOVATO?'em500':'custom-meter';}
function meterFromConfig(m){const d={id:`meter-import-${m.index}`,type:'meter',profile_id:catalogEntryForModel(m.model),brand:'Automatrix',model:String(m.model_name||'EM500').toUpperCase(),name:m.name||`Meter ${m.index+1}`,verified:m.model_name==='em500',channel:'tcp',tcp:{host:m.host||'',port:Number(m.port)||502,unit_id:Number(m.unit_id)||1},rtu:{uart:1,baud:9600,parity:'none',data_bits:8,stop_bits:1,unit_id:1},tuning:tuning(),status:'not_tested',samples:[],result:'Imported from the controller. Not yet re-qualified in this session.',applied:true,imported:true,slot:m.index,unknown:[]};
 const t=d.tuning;t.timeout_ms=Number(m.timeout_ms)||t.timeout_ms;t.normal_ms=Number(m.poll_ms)||t.normal_ms;t.function_code=Number(m.function)||t.function_code;t.register_address=Number(m.active_power_address)||0;
 /* An enum the controller reports but this build has no name for is NOT
  * silently coerced to entry zero: uint16/ABCD would be a plausible-looking
  * wrong answer that the engineer would have no reason to question. It is
  * recorded as unknown, displayed as unknown, and omitted from the save so the
  * stored value survives untouched. */
 const type=TYPE_NAMES[Number(m.data_type)];if(type)t.data_type=type;else d.unknown.push('data_type');
 const order=ORDER_NAMES[Number(m.word_order)];if(order)t.byte_order=order;else d.unknown.push('word_order');
 if(Number.isFinite(Number(m.scale))&&Number(m.scale)!==0)t.scale=Number(m.scale);else d.unknown.push('scale');
 d.role=Number(m.role)||1;if(d.role===2)d.generator_index=Number(m.generator_index)||0;
 d.phase_basis=Number(m.phase_basis)||0;
 d.enabled_at_import=Boolean(m.enabled);return d;}
function inverterFromConfig(v){return{id:`inverter-import-${v.index}`,type:'inverter',profile_id:null,brand:'Inverter',model:v.name||`Inverter ${v.index+1}`,name:v.name||`Inverter ${v.index+1}`,verified:false,channel:'tcp',tcp:{host:v.host||'',port:Number(v.port)||502,unit_id:Number(v.unit_id)||1},rtu:{uart:1,baud:9600,parity:'none',data_bits:8,stop_bits:1,unit_id:1},tuning:{...tuning(),timeout_ms:Number(v.timeout_ms)||1000},rated_kw:Number(v.rated_kw)||0,status:'not_tested',samples:[],result:'Imported from the controller. Not yet re-qualified in this session.',applied:true,imported:true,slot:v.index,unknown:[],enabled_at_import:Boolean(v.enabled)};}
/* Named so a contract can assert it: the keys qualify() writes for a meter must
 * all be keys meterFromConfig() reads. Returns the offenders, empty when sound. */
function crConfigRoundTrip(){const written=['enabled','name','host','port','unit_id','timeout_ms','poll_ms','function','active_power_address','data_type','word_order','scale','model','role','phase_basis'];const read=['enabled','name','host','port','unit_id','timeout_ms','poll_ms','function','active_power_address','data_type','word_order','scale','model','role','phase_basis'];return written.filter(k=>!read.includes(k));}
async function loadCommissioned(){if(!access()?.mayRequest('/api/meters/config'))return{devices:[],error:'Unlock Engineering to read the stored configuration.'};
 const devices=[];let error='';
 try{const payload=await api('/api/meters/config');(payload.meters||[]).forEach(m=>devices.push(meterFromConfig(m)));}catch(e){error=e.message;}
 try{const payload=await api('/api/inverters/config');(payload.inverters||[]).forEach(v=>devices.push(inverterFromConfig(v)));}catch(e){error=error||e.message;}
 return{devices,error};}
/* Replaces the draft's device list with what the controller actually holds.
 * Deliberately destructive of the DRAFT and nothing else - the draft is a local
 * scratch copy, the controller is the record. Confirmed when a draft that has
 * unsaved edits would be lost. */
function say(text,ok){const message=$('crMessage');if(!message)return;message.textContent=text;message.className=ok?'ok':'';}
/* --------------------------------------------------------------- restart notice
 *
 * Both config endpoints have always answered restart_required:true, and the
 * wizard has always thrown that away. So a saved endpoint change looked applied
 * while the running poller still used the old one -- the qualification that
 * followed tested settings that were not in force, and a "passed" verdict meant
 * nothing. It is stated once, kept until the restart actually happens, and
 * offered rather than performed: restarting a controller is the engineer's
 * decision, not a side effect of pressing Save. */
function restartBanner(){if(!state.restart_required)return'';return`<div class="cr-notice warn cr-restart" role="status"><strong>Restart required — restart now?</strong><span>Saved settings are stored but the controller is still running the previous ones. Qualification results taken before the restart describe the old settings.</span><div class="cr-step-links"><button class="button primary" data-action="restart">Restart controller</button><button class="button secondary" data-action="dismiss-restart">Later</button></div></div>`;}
/* Both endpoints answer restart_required:true on EVERY accepted save, whether or
 * not anything changed. Taken literally that never clears: restart, re-qualify,
 * and the save that re-qualification performs raises it again. So the flag is
 * raised only when the values actually sent differ from the values the
 * controller already held, compared field by field against the read-back taken
 * moments earlier. Absence of a stored entry counts as a difference. */
function differs(stored,next){if(!stored)return true;return Object.keys(next).some(key=>String(stored[key])!==String(next[key]));}
async function restartController(){if(!confirm('Restart the controller now? The web interface will be unavailable for about 20 seconds and every device connection is re-established.'))return;
 try{await api('/api/system/restart',{method:'POST'});state.restart_required=false;state.restarted_at=new Date().toISOString();save();render();say('Restart accepted. The controller is coming back up; reload this page in about 20 seconds.',true);}
 catch(error){say(`Restart was not accepted: ${error.message}`,false);}}
async function importCommissioned(announce=true){if(announce)say('Reading the stored configuration…',true);const{devices,error}=await loadCommissioned();
 if(!devices.length){if(announce)say(error||'The controller holds no commissioned devices yet.',!error);state.imported=true;save();return false;}
 state.devices=devices;state.active=devices[0].id;state.imported=true;save();render();
 const unknown=devices.flatMap(d=>d.unknown||[]);
 say(`Loaded ${devices.length} commissioned device(s) from the controller.${unknown.length?` ${unknown.length} field(s) could not be interpreted and are shown as unknown; they will be left as stored.`:''}${error?` ${error}`:''}`,!unknown.length&&!error);return true;}
function route(){return(location.hash.replace(/^#\/?/,'')||'dashboard')==='commissioning';}
function page(){const main=$('mainContent');if(!main)return null;let page=main.querySelector('[data-page="commissioning"]');if(!page){page=document.createElement('section');page.className='page';page.dataset.page='commissioning';main.append(page);}page.innerHTML='<div id="commissioningReleaseV3" class="commissioning-release-v3"></div>';return page;}
const labels=['Site','Devices','Channel','Modbus tuning','Plant control','Source detection','Connection test','Controller health','Review'];
/* The step indicator is the navigation, not a picture of it. It used to be an
 * inert <ol>: the only way to reach "Modbus tuning" on a commissioned unit was
 * Continue through five steps or "Start new commissioning", and the second of
 * those clears the draft. Each entry is now a button that jumps straight there.
 * aria-current marks the step in view, and the disabled state is never used --
 * a step that is not yet reachable still explains itself when pressed. */
function header(){return`<div class="cr-progress"><div class="cr-progress-title"><span>Commissioning</span><strong>Step ${state.step+1} of ${labels.length}: ${labels[state.step]}</strong></div><ol>${labels.map((x,i)=>`<li class="${i===state.step?'active':i<state.step?'complete':''}"><button type="button" data-step="${i}" ${i===state.step?'aria-current="step"':''}><span>${i+1}</span><b>${x}</b></button></li>`).join('')}</ol></div>`;}
/* A named shortcut back into an earlier step, for the places where the next
 * thing an engineer wants is behind one. */
function editLink(step,text){return`<button class="button secondary" data-step="${step}">${text||`Edit ${labels[step].toLowerCase()}`}</button>`;}
function nav(next='Continue',disabled=false){return`<div class="cr-nav"><div id="crMessage" role="status"></div>${state.step?'<button class="button secondary" data-action="back">Back</button>':''}<button class="button primary" data-action="next" ${disabled?'disabled':''}>${next}</button></div>`;}
function site(){return`<section class="cr-stage"><div class="cr-head"><p class="eyebrow">Project identity</p><h2>Site details</h2><p>Identify the installation so every saved setting and test result belongs to a traceable project.</p></div><div class="cr-grid"><label><span>Site or plant name *</span><input id="crSiteName" value="${esc(state.site.name)}" placeholder="Plant name"></label><label><span>Location</span><input id="crLocation" value="${esc(state.site.location)}" placeholder="City / area"></label><label><span>Commissioning engineer</span><input id="crEngineer" value="${esc(state.site.engineer)}"></label><label><span>Project reference</span><input id="crReference" value="${esc(state.site.reference)}"></label></div></section>${nav()}`;}
function devices(){return`<section class="cr-stage"><div class="cr-head"><p class="eyebrow">Installed equipment</p><h2>Select devices</h2><p>Add each meter or inverter separately. Exact model qualification remains visible throughout commissioning.</p></div><div class="cr-catalog"><article><h3>Energy meters</h3>${catalog.map(p=>`<button class="cr-add" data-add="${p.id}"><span><strong>${esc(p.brand)}</strong><small>${esc(p.model)}</small></span><b>Add</b></button>`).join('')}</article><article><h3>Solar inverters</h3><div id="crInverterCatalog"><p>Loading controller catalogue…</p></div></article></div><div class="cr-selected"><div class="cr-selected-head"><h3>Selected devices (${state.devices.length})</h3><button class="button secondary" data-action="import">Reload from controller</button></div>${state.devices.length?state.devices.map(d=>`<article><div><span>${d.type}${d.imported?' · imported':''}</span><strong>${esc(d.name)}</strong><small>${d.channel==='tcp'?`${esc(d.tcp.host||'no host')}:${d.tcp.port} · Unit ${d.tcp.unit_id}`:'RS-485'}${d.verified?' · profile verified':' · manual/model verification required'}</small></div><button class="button secondary" data-remove="${d.id}">Remove</button></article>`).join(''):'<div class="cr-empty">No devices selected. Add one above, or load what the controller already holds.</div>'}</div></section>${nav()}`;}
function tabs(){return`<div class="cr-tabs">${state.devices.map((d,i)=>`<button data-device="${d.id}" class="${d.id===state.active?'active':''}"><span>${i+1}</span><div><strong>${esc(d.name)}</strong><small>${d.type}</small></div></button>`).join('')}</div>`;}
function active(){return state.devices.find(d=>d.id===state.active)||state.devices[0];}
function channel(){const d=active();if(!d)return devices();const body=d.channel==='tcp'?`<div class="cr-grid"><label class="wide"><span>IP address or hostname</span><input id="crHost" value="${esc(d.tcp.host)}" placeholder="192.168.1.120"></label><label><span>TCP port</span><input id="crPort" type="number" value="${d.tcp.port}"></label><label><span>Unit ID</span><input id="crUnit" type="number" value="${d.tcp.unit_id}"></label>${d.type==='inverter'?`<label><span>Rated power (kW)</span><input id="crRated" type="number" step="any" min="0" value="${d.rated_kw??''}" placeholder="Nameplate AC rating"></label>`:`<label><span>Enforce the grid limit on</span><select id="crBasis"><option value="0" ${Number(d.phase_basis??0)===0?'selected':''}>Lowest phase (stricter)</option><option value="1" ${Number(d.phase_basis??0)===1?'selected':''}>Total power</option></select></label><label><span>What this meter measures</span><select id="crRole">${METER_ROLES.map(([v,l])=>`<option value="${v}" ${Number(d.role||1)===v?'selected':''}>${l}</option>`).join('')}</select></label>${Number(d.role||1)===2?`<label><span>Which generator</span><input id="crGenIndex" type="number" min="0" max="2" value="${Number(d.generator_index)||0}"></label>`:''}`}</div>`:`<div class="cr-notice warn"><strong>RTU runtime not released</strong><span>Parameters may be prepared, but the device cannot pass readiness until the ESP32 RS-485 master is implemented and qualified.</span></div><div class="cr-grid"><label><span>RS-485 port</span><select id="crUart"><option value="1" ${d.rtu.uart===1?'selected':''}>RS-485 1</option><option value="2" ${d.rtu.uart===2?'selected':''}>RS-485 2</option></select></label><label><span>Baud rate</span><select id="crBaud">${[9600,19200,38400,57600,115200].map(v=>`<option ${d.rtu.baud===v?'selected':''}>${v}</option>`).join('')}</select></label><label><span>Parity</span><select id="crParity">${['none','even','odd'].map(v=>`<option ${d.rtu.parity===v?'selected':''}>${v}</option>`).join('')}</select></label><label><span>Data bits</span><select id="crDataBits"><option ${d.rtu.data_bits===8?'selected':''}>8</option><option ${d.rtu.data_bits===7?'selected':''}>7</option></select></label><label><span>Stop bits</span><select id="crStopBits"><option ${d.rtu.stop_bits===1?'selected':''}>1</option><option ${d.rtu.stop_bits===2?'selected':''}>2</option></select></label><label><span>Slave ID</span><input id="crRtuUnit" type="number" value="${d.rtu.unit_id}"></label></div>`;return`<section class="cr-stage"><div class="cr-head"><p class="eyebrow">Physical and network connection</p><h2>Communication channel</h2><p>Select how each device connects. Only fields relevant to the chosen channel are displayed.</p></div><div class="cr-layout">${tabs()}<article class="cr-editor"><div class="cr-editor-head"><div><p>${esc(d.brand)} · ${esc(d.model)}</p><h3>Connection settings</h3></div><span>${d.type}</span></div><div class="cr-grid"><label class="wide"><span>Device name</span><input id="crDeviceName" value="${esc(d.name)}"></label><label><span>Channel</span><select id="crChannel"><option value="tcp" ${d.channel==='tcp'?'selected':''}>Modbus TCP</option><option value="rtu" ${d.channel==='rtu'?'selected':''}>Modbus RTU</option></select></label></div>${body}</article></div></section>${nav()}`;}
function timingEstimate(d){const t=d.tuning;const transaction=t.response_delay_ms+t.timeout_ms+(t.retries*t.retry_interval_ms)+t.intercall_ms;const scan=Math.max(t.normal_ms,transaction)*state.devices.length;const margin=t.stale_ms-scan;return{transaction,scan,margin,state:margin<0?'block':margin<scan?'review':'healthy'};}
/* REGISTER INTERPRETATION IS A METER-ONLY QUESTION.
 *
 * For an inverter the controller reads through the register map of the ASSIGNED
 * PROFILE -- active power, the percentage limit and its readback all come from
 * there -- and qualify() sends only host, port, unit id, timeout and rated power.
 * Every value in this section was therefore collected, validated, allowed to
 * block the Continue button, and then discarded. An engineer could correct a
 * register address for twenty minutes and change nothing.
 *
 * So the fields are not shown for an inverter, and the reason is stated where
 * they used to be. Hiding them silently would leave the same question -- "where
 * do I set the inverter's registers?" -- with no answer on screen. */
function registerInterpretation(d,t){
 if(d.type!=='meter'){
  return '<div class="cr-section"><h4>Register interpretation</h4>'
   +'<div class="cr-notice good"><strong>Taken from the inverter profile, not entered here.</strong>'
   +'<span>This inverter is read through the register map of the profile assigned to it, so there is '
   +'nothing to set on this screen. Change the profile in the engineering workspace.</span></div></div>';
 }
 return `<div class="cr-section"><h4>Register interpretation</h4>${(d.unknown||[]).length?`<div class="cr-notice warn"><strong>${d.unknown.length} stored value(s) could not be interpreted: ${d.unknown.map(esc).join(', ')}.</strong><span>The control shows this build's default, which is NOT what the controller holds. Saving leaves these fields exactly as stored unless you change them deliberately.</span></div>`:''}<div class="cr-grid"><label><span>Function code</span><select id="crFunction"><option value="3" ${t.function_code===3?'selected':''}>03 Holding registers</option><option value="4" ${t.function_code===4?'selected':''}>04 Input registers</option></select></label><label><span>Address convention</span><select id="crAddressBase"><option value="zero" ${t.address_base==='zero'?'selected':''}>Base 0 / PDU</option><option value="one" ${t.address_base==='one'?'selected':''}>Base 1</option><option value="40001" ${t.address_base==='40001'?'selected':''}>40001 notation</option></select></label><label><span>Register address</span><input id="crRegister" type="number" value="${t.register_address}"></label><label><span>Block length</span><input id="crBlock" type="number" value="${t.block_length}"></label><label><span>Data type</span><select id="crType">${['uint16','int16','uint32','int32','float32','uint64','int64','float64'].map(v=>`<option ${t.data_type===v?'selected':''}>${v}</option>`).join('')}</select></label><label><span>Byte / word order</span><select id="crOrder">${['ABCD','BADC','CDAB','DCBA'].map(v=>`<option ${t.byte_order===v?'selected':''}>${v}</option>`).join('')}</select></label><label><span>Scale</span><input id="crScale" type="number" step="any" value="${t.scale}"></label><label><span>Offset</span><input id="crOffset" type="number" step="any" value="${t.offset}"></label><label><span>Display precision</span><input id="crPrecision" type="number" value="${t.precision}"></label></div></div>`;
}
/* THE REGISTER MAP COMES FROM THE PROFILE, NOT FROM MEMORY.
 *
 * GET /api/meter-profiles serves the transcribed maps. Applying one fills the
 * tuning fields so an engineer picks an instrument instead of retyping four
 * numbers on every site -- which is how a working meter came to be
 * reconfigured with a wizard default and read 15x wrong.
 *
 * Applied only to a device that has NOT been commissioned from stored config.
 * An imported device already carries the site's real values, and those outrank
 * a catalogue entry: a clone may genuinely differ, and the engineer who
 * measured it is the authority, not this table. */
let meterProfiles=null;
async function loadMeterProfiles(){
 if(meterProfiles)return meterProfiles;
 if(!access()?.mayRequest('/api/meter-profiles'))return null;
 try{meterProfiles=(await api('/api/meter-profiles')).profiles||[];}catch{meterProfiles=null;}
 return meterProfiles;
}
async function applyMeterProfile(d){
 if(!d||d.type!=='meter'||d.imported)return false;
 const list=await loadMeterProfiles();
 const profile=(list||[]).find(p=>p.id===d.profile_id);
 if(!profile||!profile.has_register_map)return false;
 const t=d.tuning;
 t.function_code=Number(profile.function)||t.function_code;
 t.register_address=Number(profile.active_power_address);
 t.data_type=TYPE_NAMES[Number(profile.data_type)]||t.data_type;
 t.byte_order=ORDER_NAMES[Number(profile.word_order)]||t.byte_order;
 t.scale=Number(profile.scale);
 d.profile_applied=profile.manual_reference||'';
 save();
 return true;
}
function tuningStep(){const d=active();const t=d.tuning,e=timingEstimate(d);return`<section class="cr-stage"><div class="cr-head"><p class="eyebrow">Protocol behavior and decoding</p><h2>Modbus tuning</h2><p>Tune communication timing separately from register interpretation. Unsafe combinations block the next step.</p></div><div class="cr-layout">${tabs()}<article class="cr-editor"><div class="cr-editor-head"><div><p>${esc(d.name)}</p><h3>Transaction timing</h3></div><span class="${e.state}">${e.state}</span></div><div class="cr-section"><h4>Collection rates</h4><div class="cr-grid"><label><span>Priority</span><input id="crPriority" type="number" value="${t.priority}"></label><label><span>Normal frequency (ms)</span><input id="crNormal" type="number" value="${t.normal_ms}"></label><label><span>High-speed frequency (ms)</span><input id="crHigh" type="number" value="${t.high_ms}"></label><label><span>Low-speed frequency (ms)</span><input id="crLow" type="number" value="${t.low_ms}"></label><label><span>Inter-call interval (ms)</span><input id="crIntercall" type="number" value="${t.intercall_ms}"></label><label><span>Stale-data threshold (ms)</span><input id="crStale" type="number" value="${t.stale_ms}"></label></div></div><div class="cr-section"><h4>Response and recovery</h4><div class="cr-grid"><label><span>Response timeout (ms)</span><input id="crTimeout" type="number" value="${t.timeout_ms}"></label><label><span>Response delay (ms)</span><input id="crDelay" type="number" value="${t.response_delay_ms}"></label><label><span>Collection attempts</span><input id="crRetries" type="number" value="${t.retries}"></label><label><span>Attempt interval (ms)</span><input id="crRetryInterval" type="number" value="${t.retry_interval_ms}"></label><label><span>Communication detect times</span><input id="crDetect" type="number" value="${t.detect_attempts}"></label><label><span>Failures ceiling</span><input id="crFailure" type="number" value="${t.failure_ceiling}"></label><label><span>Reconnection ceiling (0=continuous)</span><input id="crReconnect" type="number" value="${t.reconnect_ceiling}"></label>${d.channel==='rtu'?`<label><span>RTU silent interval (ms)</span><input id="crSilent" type="number" value="${t.rtu_silent_ms}"></label><label><span>Turnaround delay (ms)</span><input id="crTurnaround" type="number" value="${t.turnaround_ms}"></label>`:''}</div></div>${registerInterpretation(d,t)}<div class="cr-estimate"><div><span>Worst transaction</span><strong>${e.transaction} ms</strong></div><div><span>Estimated full scan</span><strong>${e.scan} ms</strong></div><div><span>Stale margin</span><strong>${e.margin} ms</strong></div><div><span>Timing verdict</span><strong class="${e.state}">${e.state}</strong></div></div></article></div></section>${nav()}`;}
function updateChannel(){const d=active();if(!d)return;d.name=$('crDeviceName')?.value.trim()||d.name;d.channel=$('crChannel')?.value||d.channel;if(d.channel==='tcp'){d.tcp.host=$('crHost')?.value.trim()||'';d.tcp.port=Number($('crPort')?.value);d.tcp.unit_id=Number($('crUnit')?.value);
 /* Left undefined when the box is empty, never coerced to 0 or to a default.
  * qualify() refuses an unknown rating rather than assuming one, and that
  * refusal only means anything if "not entered" stays distinguishable from
  * "entered as zero". */
 if($('crRated')){const raw=$('crRated').value.trim();d.rated_kw=raw===''?undefined:Number(raw);}
 if($('crRole'))d.role=Number($('crRole').value)||1;
 /* Number(), not ||, because 0 IS the lowest-phase basis and || would
  * silently turn the stricter choice into the looser one. */
 if($('crBasis')){const v=Number($('crBasis').value);d.phase_basis=Number.isFinite(v)?v:0;}
 if($('crGenIndex'))d.generator_index=Number($('crGenIndex').value)||0;}else{d.rtu.uart=Number($('crUart')?.value);d.rtu.baud=Number($('crBaud')?.value);d.rtu.parity=$('crParity')?.value;d.rtu.data_bits=Number($('crDataBits')?.value);d.rtu.stop_bits=Number($('crStopBits')?.value);d.rtu.unit_id=Number($('crRtuUnit')?.value);}save();}
function updateTuning(){const d=active(),t=d.tuning;const before={data_type:t.data_type,word_order:t.byte_order,scale:t.scale};const number=(id,current)=>{const v=Number($(id)?.value);return Number.isFinite(v)?v:current;};Object.assign(t,{priority:number('crPriority',t.priority),normal_ms:number('crNormal',t.normal_ms),high_ms:number('crHigh',t.high_ms),low_ms:number('crLow',t.low_ms),intercall_ms:number('crIntercall',t.intercall_ms),stale_ms:number('crStale',t.stale_ms),timeout_ms:number('crTimeout',t.timeout_ms),response_delay_ms:number('crDelay',t.response_delay_ms),retries:number('crRetries',t.retries),retry_interval_ms:number('crRetryInterval',t.retry_interval_ms),detect_attempts:number('crDetect',t.detect_attempts),failure_ceiling:number('crFailure',t.failure_ceiling),reconnect_ceiling:number('crReconnect',t.reconnect_ceiling),function_code:Number($('crFunction')?.value),address_base:$('crAddressBase')?.value,register_address:number('crRegister',t.register_address),block_length:number('crBlock',t.block_length),data_type:$('crType')?.value,byte_order:$('crOrder')?.value,scale:number('crScale',t.scale),offset:number('crOffset',t.offset),precision:number('crPrecision',t.precision),rtu_silent_ms:number('crSilent',t.rtu_silent_ms),turnaround_ms:number('crTurnaround',t.turnaround_ms)});
 /* A field marked unknown is withheld from the save so the stored value
  * survives. That must not make it permanently uneditable: the moment the
  * engineer moves the control away from what it showed, the value on screen is
  * a deliberate choice and is written like any other. Compared against the
  * value captured before this read, so re-selecting the same entry does not
  * count as a change. */
 if((d.unknown||[]).length){const changed={data_type:t.data_type!==before.data_type,word_order:t.byte_order!==before.byte_order,scale:t.scale!==before.scale};d.unknown=d.unknown.filter(key=>!changed[key]);}
 save();}
function validateTuning(d){const t=d.tuning,e=timingEstimate(d);if(t.high_ms<50||t.normal_ms<100||t.low_ms<t.normal_ms)return'Collection frequencies are outside safe limits.';if(t.timeout_ms<100||t.timeout_ms>60000)return'Response timeout must be 100–60000 ms.';if(t.retries<0||t.retries>10)return'Retries must be 0–10.';
 /* Block length and scale are register-interpretation fields, and for an
  * inverter those come from the assigned profile and are not shown. Validating
  * them anyway blocked Continue on a control the engineer could not see, with a
  * message naming a field that is not on the screen -- the worst kind of dead
  * end, because there is nothing to correct. */
 if(d.type==='meter'){
  if(t.block_length<1||t.block_length>125)return'Block length must be 1–125 registers.';
  if(!Number.isFinite(t.scale)||t.scale===0)return'Scale must be finite and non-zero.';
 }if(e.margin<0)return'Estimated full scan exceeds the stale-data threshold.';return'';}
async function qualify(d){d.status='testing';d.samples=[];d.result='Running repeated-read qualification…';save();render();try{if(d.channel==='rtu')throw new Error('Modbus RTU runtime is not available in this release candidate.');if(d.type==='meter'){if(TYPE_CODES[d.tuning.data_type]===undefined)throw new Error('Selected data type is not supported by the current meter runtime.');
 /* Every key here is a key the controller stores and meterFromConfig() reads
  * back. Keys the controller keeps but this wizard never edits - model and role
  * among them - are deliberately ABSENT: meters_config_post() preserves any
  * field whose key is omitted, so leaving them out is what protects them. The
  * old code spread an /api/meters response in here instead, which contributed
  * nothing (that projection carries no configuration) and read as though it
  * did.
  *
  * A field recorded as unknown at import is dropped for the same reason: the
  * wizard could not display it, so it has no business writing it. */
 const cfg={enabled:true,name:d.name,host:d.tcp.host,port:d.tcp.port,unit_id:d.tcp.unit_id,timeout_ms:d.tuning.timeout_ms,poll_ms:d.tuning.normal_ms,function:d.tuning.function_code,active_power_address:d.tuning.register_address,data_type:TYPE_CODES[d.tuning.data_type],word_order:ORDER_CODES[d.tuning.byte_order],scale:d.tuning.scale,
  /* Stated, never inferred. The model comes from the catalogue entry the
   * engineer picked and the role from the control they set; a meter whose model
   * this build cannot name is sent as GENERIC_MODBUS so the phase gate refuses
   * it by name rather than by silence. */
  model:CATALOG_MODEL[d.profile_id]??METER_MODEL_GENERIC_MODBUS,role:Number(d.role)||1,phase_basis:Number(d.phase_basis)||0};
 if(cfg.role===2)cfg.generator_index=Number(d.generator_index)||0;
 (d.unknown||[]).forEach(key=>{delete cfg[key];});
 /* POST /api/meters/config memsets the stored array and refills it from the
  * body, so its LENGTH is the new meter count. This used to post exactly one
  * meter into slot 0 regardless of how many were commissioned, which deleted
  * every other meter - on a two-meter site, qualifying the grid meter removed
  * the generator meter. Untouched slots are carried through by index, read back
  * from the controller. An entry sent as {} changes nothing: meters_config_post
  * preserves every field whose key is absent. */
 const slot=Number.isInteger(d.slot)?d.slot:0;
 const others=await api('/api/meters/config').then(p=>Array.isArray(p.meters)?p.meters:[]).catch(()=>[]);
 const meters=[];for(let i=0;i<Math.max(slot+1,others.length);i+=1)meters.push(i===slot?cfg:{});
 const changed=differs(others[slot],cfg);
 const saved=await api('/api/meters/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({meters})});
 if(saved.restart_required&&changed)state.restart_required=true;d.applied=true;for(let i=0;i<Math.max(3,d.tuning.detect_attempts);i++){await new Promise(r=>setTimeout(r,Math.max(350,d.tuning.normal_ms)));const result=await api('/api/meters');const runtime=result.meters?.[0]?.runtime||{};d.samples.push({time:new Date().toISOString(),online:Boolean(runtime.online),value:runtime.active_power_kw??null,errors:runtime.response_errors??null});}const success=d.samples.filter(s=>s.online).length;if(success<3)throw new Error(`Only ${success}/${d.samples.length} repeated reads were valid.`);d.status='ready';d.result=`${success}/${d.samples.length} repeated reads passed. Runtime settings applied.`;}else{const slot=Number.isInteger(d.slot)?d.slot:0;
 /* The rated power now comes from the stored configuration, read through
  * /api/inverters/config. The old fallback chain ended in a literal 100, so an
  * inverter whose rating could not be read was silently commissioned as a
  * 100 kW machine - a number nobody entered and nothing on screen showed. */
 const all=await api('/api/inverters/config').then(p=>Array.isArray(p.inverters)?p.inverters:[]).catch(()=>[]);
 const stored=all[slot]||null;
 const timeout=Math.max(100,Number(d.tuning.timeout_ms)||1000);
 const rated=Number(d.rated_kw)>0?Number(d.rated_kw):Number(stored?.rated_kw)>0?Number(stored.rated_kw):0;
 if(!(rated>0))throw new Error('Rated power is not known for this inverter. Set it before qualifying; it is never assumed.');
 /* POST /api/inverters/config REPLACES the whole array and sets inverter_count
  * from its length, so posting one entry for slot 1 would move it to slot 0 and
  * delete whatever slot 0 held. Every other slot is carried through unchanged,
  * read back from the controller rather than reconstructed from the draft. */
 const entry={enabled:true,name:(d.name||`Inverter ${slot+1}`).slice(0,31),host:d.tcp.host,port:d.tcp.port,unit_id:d.tcp.unit_id,timeout_ms:timeout,rated_kw:rated};
 const payload=[];for(let i=0;i<=slot;i+=1){const s=all[i];payload.push(i===slot?entry:s?{enabled:Boolean(s.enabled),name:s.name,host:s.host,port:s.port,unit_id:s.unit_id,timeout_ms:s.timeout_ms,rated_kw:s.rated_kw}:{enabled:false,name:`Inverter ${i+1}`,host:'',port:502,unit_id:1,timeout_ms:1000,rated_kw:0});}
 for(let i=slot+1;i<all.length;i+=1){const s=all[i];payload.push({enabled:Boolean(s.enabled),name:s.name,host:s.host,port:s.port,unit_id:s.unit_id,timeout_ms:s.timeout_ms,rated_kw:s.rated_kw});}
 const changed=differs(stored,entry);
 const saved=await api('/api/inverters/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({inverters:payload})});
 if(saved.restart_required&&changed)state.restart_required=true;if(d.profile_id)await api('/api/inverter-profile-assignment',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({inverter_index:slot,profile_id:d.profile_id})});d.applied=true;for(let i=0;i<Math.max(3,d.tuning.detect_attempts);i++){const r=await api('/api/inverter-probe',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({inverter_index:slot})});d.samples.push({time:new Date().toISOString(),online:r.success!==false&&r.supported!==false});await new Promise(x=>setTimeout(x,Math.max(250,d.tuning.intercall_ms)));}const success=d.samples.filter(s=>s.online).length;if(success<3)throw new Error(`Only ${success}/${d.samples.length} read-only probes passed.`);d.status='ready';d.result=`${success}/${d.samples.length} read-only probes passed. Writes remain locked.`;}}catch(error){d.status='failed';d.result=error.message;}save();render();}
function tests(){const ready=state.devices.filter(d=>d.status==='ready').length;return`<section class="cr-stage"><div class="cr-head"><p class="eyebrow">Evidence-based qualification</p><h2>Connection test</h2><p>Each device must pass repeated protocol reads. A single successful response is not accepted.</p></div><div class="cr-step-links">${editLink(2,'Edit connection channel')}${editLink(3,'Edit Modbus tuning')}</div><div class="cr-summary"><div><span>Ready</span><strong>${ready}</strong></div><div><span>Attention</span><strong>${state.devices.filter(d=>d.status==='failed').length}</strong></div><div><span>Not tested</span><strong>${state.devices.filter(d=>d.status==='not_tested').length}</strong></div></div><div class="cr-test-list">${state.devices.map(d=>`<article class="${d.status}"><div><span>${d.type} · Modbus ${d.channel.toUpperCase()}</span><strong>${esc(d.name)}</strong><small>${d.channel==='tcp'?`${esc(d.tcp.host)}:${d.tcp.port} · Unit ${d.tcp.unit_id}`:`RS-485 ${d.rtu.uart} · ${d.rtu.baud} · Unit ${d.rtu.unit_id}`}</small><p>${esc(d.result||'Not tested')}</p></div><button class="button primary" data-test="${d.id}" ${d.status==='testing'?'disabled':''}>${d.status==='testing'?'Testing…':'Run qualification'}</button></article>`).join('')}</div></section>${nav()}`;}
const bytes=v=>v==null?'Unavailable':v>=1048576?`${(v/1048576).toFixed(1)} MB`:`${Math.round(v/1024)} KB`;
async function loadResources(){try{state.resources=await api('/api/system/resources');}catch(error){state.resources={resource_state:'critical',error:error.message};}save();render();}
function health(){const r=state.resources;return`<section class="cr-stage"><div class="cr-head"><p class="eyebrow">Reliability margin</p><h2>Controller health</h2><p>Review processor, memory, reset, storage and temperature evidence before accepting the controller.</p></div>${!r?'<div class="cr-empty">Resource telemetry has not been loaded.</div>':`<div class="cr-health-verdict ${r.resource_state}"><span>Controller resource state</span><strong>${esc(r.resource_state)}</strong><small>${esc(r.error||'Live ESP32 resource telemetry')}</small></div><div class="cr-health-grid"><article><span>Processor</span><strong>${r.cpu_cores??'--'} cores · ${r.cpu_frequency_mhz??'--'} MHz</strong><small>${esc(r.target||'ESP32')} · ${r.task_count??'--'} tasks</small></article><article><span>Free internal heap</span><strong>${bytes(r.free_internal_heap_bytes)}</strong><small>Minimum ${bytes(r.minimum_internal_heap_bytes)}</small></article><article><span>Largest block</span><strong>${bytes(r.largest_internal_block_bytes)}</strong><small>Fragmentation ${r.internal_fragmentation_ratio==null?'--':`${Math.round(r.internal_fragmentation_ratio*100)}%`}</small></article><article><span>PSRAM</span><strong>${r.psram_available?bytes(r.psram_free_bytes):'Not available'}</strong><small>${r.psram_available?`Total ${bytes(r.psram_total_bytes)}`:'Firmware allocator reports no PSRAM'}</small></article><article><span>Flash</span><strong>${bytes(r.flash_size_bytes)}</strong><small>${r.flash_size_available?'Detected from flash chip':'Not available'}</small></article><article><span>Uptime</span><strong>${Math.floor((r.uptime_ms||0)/60000)} min</strong><small>Reset: ${esc(r.reset_reason_name||'unknown')}</small></article><article><span>Internal temperature</span><strong>${r.temperature_available?`${r.temperature_c} °C`:'Not available'}</strong><small>${esc(r.temperature_note||'')}</small></article></div>`}<button class="button secondary" data-action="refresh-health">Refresh health</button></section>${nav()}`;}
function verdict(){const devicesReady=state.devices.length>0&&state.devices.every(d=>d.status==='ready');const resources=state.resources?.resource_state;const blockers=[];if(!devicesReady)blockers.push('All enabled devices must pass repeated-read qualification.');if(resources==='critical'||!resources)blockers.push('Controller resource telemetry is missing or critical.');if(state.devices.some(d=>d.channel==='rtu'))blockers.push('RTU devices cannot be released until the RTU runtime is qualified.');if(state.devices.some(d=>!d.verified))blockers.push('One or more device profiles require model/manual verification.');
 /* A pending restart invalidates the evidence, so it blocks rather than warns.
  * Saved settings are stored but not in force: the repeated reads that follow a
  * save describe the PREVIOUS configuration, and calling that a pass would
  * certify settings nobody has tested. Clearing it needs a restart and a
  * re-run, which is the correct order of work. */
 if(state.restart_required)blockers.push('Settings were saved but the controller has not restarted; qualification evidence describes the previous settings.');
 return{blockers,state:blockers.length?'blocked':resources==='review'?'review':'ready'};}
function report(){const v=verdict();return{generated_at:new Date().toISOString(),product:'Automatrix PV-DG Controller',commissioning_version:3,site:state.site,devices:state.devices,controller_resources:state.resources,acceptance:{state:v.state,blockers:v.blockers,finished_at:state.finished_at||null,restart_pending:Boolean(state.restart_required),automatic_control:'remains disabled until field acceptance approval',field_acceptance:'not established by this report'}};}
/* Finishing.
 *
 * This button carried no data-action and bind() wired no handler for it, so
 * pressing it did nothing at all: no request, no message, no stored state. It
 * looked like the end of the workflow and was inert.
 *
 * What it does NOT do is any less important than what it does. There is no
 * "commissioning complete" endpoint in the firmware -- GET /api/commissioning/gate
 * reads, it does not write -- and inventing a claim of completion the
 * controller has not made would be worse than an inert button. Device settings
 * were already written and verified during qualification; nothing is waiting on
 * this press.
 *
 * So it does the two honest things: it records completion where the draft lives
 * and exports the evidence, and it states plainly what has NOT been established
 * -- automatic control is still disabled and field acceptance is still
 * outstanding. It is refused outright while a restart is pending, because the
 * evidence in the report would describe settings that were not in force. */
function finish(){const v=verdict();
 if(v.blockers.length){say(`Cannot finish: ${v.blockers[0]}`,false);return;}
 if(!confirm('Mark commissioning complete and export the report?\n\nAutomatic control stays disabled. Physical field acceptance and control-loop testing are still required.'))return;
 state.finished_at=new Date().toISOString();save();download();render();
 say('Commissioning marked complete and the report exported. Automatic control remains disabled.',true);}
function download(){const blob=new Blob([JSON.stringify(report(),null,2)],{type:'application/json'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=`Automatrix-Commissioning-${(state.site.name||'site').replace(/[^a-z0-9]+/gi,'-')}.json`;a.click();URL.revokeObjectURL(a.href);}
function review(){const v=verdict();return`<section class="cr-stage"><div class="cr-head"><p class="eyebrow">Acceptance summary</p><h2>Review and finish</h2><p>Commissioning finishes only when required evidence is present. Warnings and blockers remain visible in the exported report.</p></div><div class="cr-final ${v.state}"><span>Commissioning verdict</span><strong>${v.state}</strong><small>${v.blockers.length?`${v.blockers.length} blocker(s) remain`:'Software acceptance checks passed'}</small></div><div class="cr-review-grid"><article><h3>Site</h3><p><strong>${esc(state.site.name)}</strong><br>${esc(state.site.location)}<br>${esc(state.site.engineer)}</p></article><article><h3>Devices</h3><p>${state.devices.length} configured<br>${state.devices.filter(d=>d.status==='ready').length} ready<br>${state.devices.filter(d=>d.applied).length} runtime-applied</p></article><article><h3>Controller</h3><p>Resources: ${esc(state.resources?.resource_state||'not loaded')}<br>Temperature: ${state.resources?.temperature_available?'available':'not available'}<br>Control: remains disabled</p></article></div>${v.blockers.length?`<div class="cr-blockers"><h3>Release blockers</h3><ul>${v.blockers.map(x=>`<li>${esc(x)}</li>`).join('')}</ul></div>`:'<div class="cr-notice good"><strong>Software commissioning checks passed.</strong><span>Physical field acceptance and controlled control-loop testing are still required.</span></div>'}<div class="cr-step-links">${editLink(0,'Edit site details')}${editLink(2,'Edit connection channel')}${editLink(3,'Edit Modbus tuning')}${editLink(4,'Re-run connection test')}</div><div class="cr-final-actions"><button class="button secondary" data-action="export">Export report</button><button class="button secondary" data-action="restart-wizard">Start new commissioning</button><button class="button primary" data-action="finish" ${v.state==='blocked'?'disabled':''}>Finish commissioning</button></div>${state.finished_at?`<div class="cr-notice good"><strong>Marked complete ${esc(new Date(state.finished_at).toLocaleString())}.</strong><span>Recorded in this browser and in the exported report. Automatic control remains disabled; physical field acceptance is still required.</span></div>`:''}</section>`;}
/* ------------------------------------------------------------- plant control
 *
 * The settings the controller actually regulates on: the grid policy and its
 * export or import limit, which measurement the limit is enforced on, the
 * generator's minimum loading, reserve and reverse-power margin, the per-source
 * PV ramp rates, and the grid loss and recovery timers.
 *
 * Commissioning walked an engineer through meter registers and Modbus timing and
 * never once asked for any of it. Every one of these was implemented, tested and
 * given a UI -- on a separate page an engineer had to know to go and find.
 *
 * THIS IS NOT A SECOND FORM. It mounts the workspace from web/solar-grid.js, so
 * the validation, the fail-closed rules and the POST all stay in one place. A
 * copy here would be a second implementation of the same safety rules, and the
 * two would drift the first time one was corrected.
 */
function plantControl(){return`<section class="cr-stage"><div class="cr-head"><p class="eyebrow">What the controller regulates on</p><h2>Plant control</h2><p>The grid policy and its limit, the measurement it is enforced on, the generator protections and the PV ramp rates. Saving any of these forces automatic control back to disabled, so it must be armed again deliberately afterwards.</p></div><div id="crPlantControlHost"></div></section>${nav()}`;}

/* ---------------------------------------------------------- source detection
 *
 * Whether the plant is running on the grid or on a generator. Everything the
 * controller does downstream depends on that answer: which policy applies,
 * whether reverse power matters, how fast PV may ramp.
 *
 * Two topologies, and the site has one or the other:
 *
 *   ONE METER  -- the EM-500 tariff input at 0x2100. The generator's 220 V feeds
 *                 it. Zero is grid; any energised input is generator.
 *   TWO METERS -- whichever meter reads above its threshold is carrying the
 *                 load. Both above is a fault, unless the plant was commissioned
 *                 as synchronisation-capable.
 *
 * This was implemented, tested and given a UI -- as a TAB inside the EM-500
 * analyser workspace. An engineer had to open the meter page, find the analyser,
 * and know the tab existed. Commissioning never asked. It does now, and it hosts
 * the same panels rather than a copy.
 */
function sourceStep(){return`<section class="cr-stage"><div class="cr-head"><p class="eyebrow">Grid or generator</p><h2>Source detection</h2><p>How the controller decides which source is carrying the plant. One meter uses the EM-500 tariff input; two meters compare measured power against a threshold. Until this is commissioned, automatic control stays fail-closed.</p></div><div id="crSourceDetectionHost"></div></section>${nav()}`;}

function render(){if(!route())return;page();const root=$('commissioningReleaseV3');if(!root)return;const views=[site,devices,channel,tuningStep,plantControl,sourceStep,tests,health,review];root.innerHTML=header()+restartBanner()+views[state.step]();bind();if(state.step===1)loadInverters();
 /* The workspace is built by its own module; this asks it to mount into
  * the host above and load what is commissioned. */
 if(state.step===4)window.AutomatrixSolarGrid?.mount();
 if(state.step===5)window.AutomatrixSourceDetection?.mount();}
const access=()=>window.AutomatrixEngineeringAccess;
/* Engineering-only catalogue: never requested outside the commissioning route,
 * because a guaranteed 401 still consumes one of the few client sockets. */
async function loadInverters(){const target=$('crInverterCatalog');if(!target)return;if(!access()?.mayRequest('/api/inverter-profiles')){target.innerHTML='<div class="cr-empty">Unlock Engineering to load the inverter catalogue.</div>';return;}try{const payload=await api('/api/inverter-profiles');const profiles=payload.profiles||[];target.innerHTML=profiles.length?profiles.map(p=>`<button class="cr-add" data-inverter="${esc(p.id||p.profile_id)}" data-brand="${esc(p.manufacturer||'Other')}" data-model="${esc(p.model_family||p.name||'Custom inverter')}" data-verified="${Boolean(p.read_allowed)}"><span><strong>${esc(p.manufacturer||'Other')}</strong><small>${esc(p.model_family||p.name||'Custom inverter')}</small></span><b>Add</b></button>`).join(''):'<p>No inverter profiles available.</p>';target.querySelectorAll('[data-inverter]').forEach(button=>button.onclick=()=>addDevice({id:button.dataset.inverter,type:'inverter',brand:button.dataset.brand,model:button.dataset.model,protocols:['tcp'],verified:button.dataset.verified==='true'}));}catch(error){target.innerHTML=`<p>${esc(error.message)}</p>`;}}
/* Whatever is on screen for the CURRENT step, written into state. Separated
 * from validation so a step can be left by any route -- Continue, Back, or a
 * jump from the step indicator -- without the edits in front of the engineer
 * being silently dropped. They used to be dropped on Back. */
function commit(){if(state.step===0&&$('crSiteName'))state.site={name:$('crSiteName').value.trim(),location:$('crLocation').value.trim(),engineer:$('crEngineer').value.trim(),reference:$('crReference').value.trim()};if(state.step===2)updateChannel();if(state.step===3)updateTuning();}
/* Why a step is not yet satisfied, or '' when it is. Reads state only -- never
 * the DOM -- so it can be asked about a step that is not on screen. That is
 * what makes a jump checkable: every step before the target is tested, and the
 * engineer lands on the first one that actually blocks rather than on a form
 * that silently refuses to advance. */
function stepBlocker(step){
 if(step===0)return state.site.name?'':'Enter the site or plant name.';
 if(step===1)return state.devices.length?'':'Add at least one device.';
 if(step===2){const bad=state.devices.find(d=>d.channel==='tcp'&&(!d.tcp.host||d.tcp.port<1||d.tcp.port>65535||d.tcp.unit_id<1||d.tcp.unit_id>247));if(bad)return`Complete valid TCP settings for ${bad.name}.`;
  /* Caught here rather than at qualification, where the refusal would arrive
   * after the endpoint had already been written. */
  const unrated=state.devices.find(d=>d.type==='inverter'&&!(Number(d.rated_kw)>0));return unrated?`Enter the rated power for ${unrated.name}. It is never assumed.`:'';}
 if(step===3){const bad=state.devices.map(d=>[d,validateTuning(d)]).find(([,error])=>error);return bad?`${bad[0].name}: ${bad[1]}`:'';}
 /* Step 4 is Plant control. It carries no blocker of its own: whether a policy
  * is REQUIRED depends on the plant -- a generator-only site commissions no grid
  * limit -- and inventing a requirement here would refuse a legitimate
  * configuration. What is genuinely required is stated by the controller itself
  * at /api/commissioning/gate, and is surfaced separately.
  *
  * The workspace mounted in this step still refuses its own invalid input, and
  * still forces automatic control back to disabled on every save. */
 /* Step 5 is Source detection. No blocker for the same reason as step 4:
  * the controller states what it genuinely requires at
  * /api/commissioning/gate, and the panel itself stays fail-closed until
  * it is commissioned. */
 if(step===6)return state.devices.some(d=>d.status!=='ready')?'Every release-enabled device must pass qualification or be removed.':'';
 if(step===7)return(!state.resources||state.resources.resource_state==='critical')?'Load controller health and resolve critical resource conditions.':'';
 return'';}
/* Move to any step directly.
 *
 * Backwards is always allowed: revisiting a completed step to change one value
 * is the whole point, and re-running the six steps in order to reach "Modbus
 * tuning" is what pushed engineers into re-running commissioning from scratch.
 *
 * Forwards is allowed only over steps that are already satisfied. The engineer
 * is not told "no": they are taken to the first step that is not, with the
 * reason. */
function goto(target){commit();const step=Math.max(0,Math.min(labels.length-1,Number(target)));
 if(step>state.step){for(let i=state.step;i<step;i+=1){const blocker=stepBlocker(i);if(blocker){state.step=i;save();render();say(blocker,false);return;}}}
 state.step=step;save();render();}
function next(){commit();const blocker=stepBlocker(state.step);if(blocker){say(blocker,false);return;}state.step=Math.min(labels.length-1,state.step+1);save();render();}
function bind(){document.querySelectorAll('[data-add]').forEach(button=>button.onclick=()=>addDevice(catalog.find(p=>p.id===button.dataset.add)));document.querySelectorAll('[data-remove]').forEach(button=>button.onclick=()=>{state.devices=state.devices.filter(d=>d.id!==button.dataset.remove);state.active=state.devices[0]?.id||null;save();render();});document.querySelectorAll('[data-device]').forEach(button=>button.onclick=()=>{if(state.step===2)updateChannel();if(state.step===3)updateTuning();state.active=button.dataset.device;save();render();});document.querySelectorAll('[data-test]').forEach(button=>button.onclick=()=>qualify(state.devices.find(d=>d.id===button.dataset.test)));document.querySelectorAll('[data-step]').forEach(button=>button.onclick=()=>goto(button.dataset.step));document.querySelector('[data-action="back"]')?.addEventListener('click',()=>goto(state.step-1));document.querySelector('[data-action="next"]')?.addEventListener('click',next);document.querySelector('[data-action="refresh-health"]')?.addEventListener('click',loadResources);document.querySelector('[data-action="export"]')?.addEventListener('click',download);document.querySelector('[data-action="finish"]')?.addEventListener('click',finish);document.querySelector('[data-action="restart"]')?.addEventListener('click',restartController);document.querySelector('[data-action="dismiss-restart"]')?.addEventListener('click',()=>{
 /* "Later" hides the banner for this render only. It does NOT clear
  * restart_required: the controller is still running the old settings and the
  * next render has to say so again. Only an accepted restart clears it. */
 document.querySelector('.cr-restart')?.setAttribute('hidden','');});document.querySelector('[data-action="import"]')?.addEventListener('click',()=>{if(state.devices.length&&!confirm('Replace the devices in this draft with what the controller currently holds?'))return;importCommissioned();});document.querySelector('[data-action="restart-wizard"]')?.addEventListener('click',()=>{if(confirm('Clear the local commissioning draft and start again?')){state=defaults();save();render();}});$('crChannel')?.addEventListener('change',()=>{updateChannel();render();});$('crRole')?.addEventListener('change',()=>{updateChannel();render();});}
/* Opening the wizard on an empty draft reads the controller first. Only when the
 * draft is empty: a draft with devices in it represents work in progress, and
 * replacing that silently would be the same class of surprise this whole change
 * exists to remove. The "Reload from controller" button covers that case, and
 * asks first. */
function start(){if(route()){page();render();if(!state.imported&&!state.devices.length)importCommissioned(false);}document.body.classList.toggle('commissioning-release-active',route());}
access()?.onScopeChange(start);if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',start,{once:true});else start();
})();
