#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute actual SO scope/state helpers with checked Vulkan call captures.

This CPU contract is separate from native numerical/validation qualification.
"""
from pathlib import Path
import os,re,shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
inc=(r/'hw/xbox/nv2a/pgraph/vk/shader-object.inc').read_text()
def function(text,name):
    m=re.search(r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_]\w*(?:\s*\*)?\s*\b'+name+r'\([^;{]*\)\s*\{',text,re.M);assert m,name
    i=text.index('{',m.start())+1;depth=1
    while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[m.start():i]
names=['shader_objects_unbind','shader_objects_begin_rendering','shader_objects_bind_draw']
parts={n:function(inc,n) for n in names};parts['end_render_pass']=function(draw,'end_render_pass');names+=['end_render_pass']
command=function(draw,'pgraph_vk_begin_command_buffer')
boundary=re.search(r'    r->dyn_last.valid = false;\s*    r->vi_last.valid = false;',command)[0]
parts['new_command_state_boundary']='static void new_command_state_boundary(PGRAPHVkState *r){\n'+boundary+'\n}'
names+=['new_command_state_boundary']
constants=(r/'hw/xbox/nv2a/pgraph/vk/constants.h').read_text()
polygon_map=re.search(r'static const VkPolygonMode pgraph_polygon_mode_vk_map\[\] = \{.*?\};',constants,re.S)[0]
vsh_regs=(r/'hw/xbox/nv2a/pgraph/vsh_regs.h').read_text()
polygon_enum=re.search(r'enum ShaderPolygonMode \{.*?\};',vsh_regs,re.S)[0]
polygon_map=polygon_enum+'\n'+polygon_map
template=Path(__file__).with_suffix('.c').read_text().replace('/* POLYGON_MAP */',polygon_map)
def wiring(text):
    create=function(text,'create_pipeline');so=create[:create.index('// FIXME:')]
    assert so.index('pgraph_vk_bind_shaders(pg);')<so.index('pgraph_clear_dirty_reg_map(pg);')<so.index('return;')
    pre=function(text,'begin_pre_draw')
    assert 'r->shader_objects.rendering != (r->shader_objects.draw && !pg->clearing)' in pre
    assert 'r->framebuffer_dirty || render_pass_dirty || scope_changed' in pre
    descriptor=pre.index('pgraph_vk_update_descriptor_sets(pg);')
    assert 'pgraph_vk_ensure_command_buffer(pg);' in pre[descriptor:]
    begin=function(text,'begin_render_pass')
    assert begin.index('shader_objects_unbind(r);')<begin.index('vkCmdBeginRenderPass(')
    normal=function(text,'begin_draw')
    assert normal.index('vk_cost_materialize(pg);')<normal.index('shader_objects_bind_draw(pg);')
    assert normal.index('shader_objects_unbind(r);')<normal.index('vkCmdBindPipeline(')
    dynamic=function(text,'emit_dynamic_state')
    assert dynamic.count('r->dyn_cull_front || r->shader_objects.draw')==2
    assert dynamic.count('r->dyn_blend || r->shader_objects.draw')==1
    for call in ['vkCmdSetDepthWriteEnableEXT(','vkCmdSetDepthCompareOpEXT(','vkCmdSetColorBlendEquationEXT(','vkCmdSetStencilOpEXT(']:assert call in dynamic
    assert '!r->shader_objects.draw || r->shader_objects.features.depthBounds' in dynamic
    vi=function(text,'emit_dynamic_vertex_input');assert 'pgraph_vk_ready_vertex_input(pg);' in vi
wiring(draw)
recording_recheck = re.search(r'pgraph_vk_ensure_command_buffer\(pg\);\n    if \([^\n]*r->framebuffer == VK_NULL_HANDLE\)', function(draw, 'begin_pre_draw'))[0]
wiring_controls=[
 ('dirty map omitted','pgraph_clear_dirty_reg_map(pg);','/* omitted dirty map */',True),
 ('dirty map cleared before uniform consumption','pgraph_vk_bind_shaders(pg);','pgraph_clear_dirty_reg_map(pg); pgraph_vk_bind_shaders(pg);',True),
 ('identical RP handle conceals mode change','r->framebuffer_dirty || render_pass_dirty || scope_changed','r->framebuffer_dirty || render_pass_dirty',False),
 ('scope compares capability rather than active draw','r->shader_objects.rendering != (r->shader_objects.draw && !pg->clearing)','r->shader_objects.rendering != r->shader_objects.enabled',False),
 ('descriptor rotation leaves command buffer absent',recording_recheck,recording_recheck.replace('pgraph_vk_ensure_command_buffer(pg);','/* omitted recording recheck */'),False),
 ('draw blend state gated by ordinary capability','r->dyn_blend || r->shader_objects.draw','r->dyn_blend',False),
 ('draw depth state gated by ordinary capability','r->dyn_cull_front || r->shader_objects.draw','r->dyn_cull_front',False),
 ('unsupported depthBounds command','!r->shader_objects.draw || r->shader_objects.features.depthBounds','true',False),
]
for label,before,after,prefix in wiring_controls:
    target=function(draw,'create_pipeline') if prefix else draw;assert before in target,label
    target=target.replace(before,after,1)
    mutant=draw.replace(function(draw,'create_pipeline'),target) if prefix else target
    try:wiring(mutant)
    except (AssertionError,ValueError):pass
    else:raise AssertionError('wiring control accepted: '+label)
controls=[
 ('stage binding omitted','shader_objects_bind_draw','vkCmdBindShadersEXT(r->command_buffer, n, stages, objects);','(void)stages; (void)objects; (void)n;'),
 ('NULL geometry stage retains stale object','shader_objects_bind_draw','geom ? geom->shader_object : VK_NULL_HANDLE','geom ? geom->shader_object : vsh->shader_object'),
 ('tessellation stage omitted','shader_objects_bind_draw','if (r->shader_objects.features.tessellationShader) {','if (false && r->shader_objects.features.tessellationShader) {'),
 ('viewport scaling omitted','shader_objects_bind_draw','pgraph_apply_scaling_factor(pg, &width, &height);','/* no scaling */'),
 ('scissor AA omitted','shader_objects_bind_draw','pgraph_apply_anti_aliasing_factor(pg, &w, &h);','/* no AA */'),
 ('sample mask clobbered','shader_objects_bind_draw','VkSampleMask mask = ~0u;','VkSampleMask mask = 0;'),
 ('ordinary raster depth-clamp mismatch','shader_objects_bind_draw','vkCmdSetDepthClampEnableEXT(r->command_buffer, VK_TRUE);','vkCmdSetDepthClampEnableEXT(r->command_buffer, VK_FALSE);'),
 ('mandatory dynamic emission not forced','shader_objects_bind_draw','emit_dynamic_state(pg, force);','emit_dynamic_state(pg, false);'),
 ('fragment native owner not retained','shader_objects_bind_draw','pgraph_vk_shader_object_use(r, psh);','/* no FS use */'),
 ('utility leaves object stage bound','shader_objects_unbind','r->shader_objects.bound = false;','/* still bound */'),
 ('utility retains stale vertex-input cache','shader_objects_unbind','r->dyn_last.valid = r->vi_last.valid = false;','r->dyn_last.valid = false;'),
 ('attachment pixels discarded','shader_objects_begin_rendering','.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD','.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE'),
 ('stencil aspect not attached','shader_objects_begin_rendering','(r->zeta_binding->host_fmt.aspect & VK_IMAGE_ASPECT_STENCIL_BIT)','(r->zeta_binding->host_fmt.aspect & VK_IMAGE_ASPECT_DEPTH_BIT)'),
 ('attachment dependency loses writes','shader_objects_begin_rendering','VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |','0 |'),
 ('cost queries survive pass','end_render_pass','vk_cost_close_recorded_queries(r);','/* cost query remains */'),
 ('dynamic rendering flag survives end','end_render_pass','r->shader_objects.rendering = false;','/* still rendering */'),
 ('cached draws redundantly force all state','shader_objects_bind_draw','bool force = !r->shader_objects.bound || !r->dyn_last.valid;','bool force = true;'),
 ('new binding ignores invalid bound owner','shader_objects_bind_draw','bool force = !r->shader_objects.bound || !r->dyn_last.valid;','bool force = !r->dyn_last.valid;'),
 ('new command ignores invalid dynamic owner','shader_objects_bind_draw','bool force = !r->shader_objects.bound || !r->dyn_last.valid;','bool force = !r->shader_objects.bound;'),
 ('new command retains dynamic snapshot','new_command_state_boundary','r->dyn_last.valid = false;','/* stale dynamic snapshot */'),
 ('new command retains vertex snapshot','new_command_state_boundary','r->vi_last.valid = false;','/* stale vertex snapshot */'),
 ('cached draw skips late vertex input','shader_objects_bind_draw','emit_dynamic_vertex_input(pg);','if (force) emit_dynamic_vertex_input(pg);'),
 ('cached draw skips changing polygon','shader_objects_bind_draw','vkCmdSetPolygonModeEXT(r->command_buffer,','if (force) vkCmdSetPolygonModeEXT(r->command_buffer,'),
 ('cached draw skips changing viewport','shader_objects_bind_draw','vkCmdSetViewportWithCountEXT(r->command_buffer, 1, &viewport);','if (force) vkCmdSetViewportWithCountEXT(r->command_buffer, 1, &viewport);'),
 ('unchanged recording redundantly rebinds stages','shader_objects_bind_draw','if (force || r->shader_bindings_changed) {','if (true) {'),
 ('changed stage tuple inherits old native binding','shader_objects_bind_draw','if (force || r->shader_bindings_changed) {','if (force) {'),
 ('new recording misses retained native owners','shader_objects_bind_draw','if (force || r->shader_bindings_changed) {','if (r->shader_bindings_changed) {'),
]
env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-so-state-') as tmp:
    p=Path(tmp);source=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-I'+str(r),'-I'+str(r/'deps/vulkan-headers'),str(source),'-o',str(exe)]
    def run(parts,negative=False):
        source.write_text(template.replace('/* PRODUCTION */','\n'.join(parts[n] for n in names)))
        subprocess.run(cmd,env=env,check=True)
        result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=60)
        if negative:assert result.returncode!=0 and re.search('assertion|Assertion',result.stderr),(result.returncode,result.stdout,result.stderr)
        else:assert result.returncode==0,(result.returncode,result.stdout,result.stderr);print(result.stdout,end='',flush=True)
    run(parts)
    for label,name,before,after in controls:
        assert before in parts[name],label
        changed=dict(parts);changed[name]=parts[name].replace(before,after,1);run(changed,True)
        print('required numerical/API-capture control rejected:',label,flush=True)
print(f'{len(controls)} executed behavioral controls and {len(wiring_controls)} source-consumer controls PASS',flush=True)

# Independently execute the actual complete register-driven emitter. The scope
# helper above uses an ordering adapter; this second binary checks its values.
dynamic_template=Path(__file__).with_name('test-vk-shader-object-dynamic.c').read_text()
constants=(r/'hw/xbox/nv2a/pgraph/vk/constants.h').read_text()
maps='\n'.join(re.search(r'static const [^;]+ '+n+r'\[\] = \{.*?\};',constants,re.S)[0] for n in ['pgraph_blend_factor_vk_map','pgraph_blend_equation_vk_map','pgraph_cull_face_vk_map','pgraph_depth_func_vk_map','pgraph_stencil_func_vk_map','pgraph_stencil_op_vk_map'])
dynamic=function(draw,'emit_dynamic_state');write=function(draw,'color_write_mask')
dynamic_controls=[
 ('SO ignores blend when EDS flags false','r->dyn_blend || r->shader_objects.draw','r->dyn_blend'),
 ('SO ignores depth/cull when EDS flags false','r->dyn_cull_front || r->shader_objects.draw','r->dyn_cull_front'),
 ('mandatory depth write omitted','vkCmdSetDepthWriteEnableEXT(\n            cmd, !!(control_0 & NV_PGRAPH_CONTROL_0_ZWRITEENABLE));','(void)vkCmdSetDepthWriteEnableEXT;'),
 ('mandatory blend equation omitted','vkCmdSetColorBlendEquationEXT(cmd, 0, 1, &eq);','(void)vkCmdSetColorBlendEquationEXT; (void)eq;'),
 ('disabled feature gets depth-bounds command','!r->shader_objects.draw || r->shader_objects.features.depthBounds','true'),
 ('stencil pass and depth-fail swapped','pgraph_stencil_op_vk_map[zpass],\n                                 pgraph_stencil_op_vk_map[zfail]','pgraph_stencil_op_vk_map[zfail],\n                                 pgraph_stencil_op_vk_map[zpass]'),
 ('force fails to restore state after PSO','if (!force && r->dyn_last.valid','if ((force || !force) && r->dyn_last.valid'),
 ('utility clear does not invalidate state','r->dyn_last.valid = false;','/* utility cache remains valid */'),
 ('cached SO draw ignores changed registers','r->dyn_last.control_0 == control_0','true'),
]
with tempfile.TemporaryDirectory(prefix='vk-so-dynamic-') as tmp:
    p=Path(tmp);source=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(r),'-I'+str(r/'deps/vulkan-headers'),str(source),'-o',str(exe)]
    def run_dynamic(body,negative=False):
        source.write_text(dynamic_template.replace('/* MAPS */',maps).replace('/* PRODUCTION */',write+'\n'+body))
        subprocess.run(cmd,env=env,check=True)
        result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=60)
        if negative:assert result.returncode!=0 and re.search('assertion|Assertion',result.stderr),(result.returncode,result.stdout,result.stderr)
        else:assert result.returncode==0,(result.returncode,result.stdout,result.stderr);print(result.stdout,end='',flush=True)
    run_dynamic(dynamic)
    for label,before,after in dynamic_controls:
        assert before in dynamic,label;run_dynamic(dynamic.replace(before,after,1),True)
        print('required actual-emitter control rejected:',label,flush=True)
print(f'{len(dynamic_controls)} actual-emitter controls PASS',flush=True)

input_template=Path(__file__).with_name('test-vk-shader-object-input.c').read_text()
input_body=function(draw,'emit_dynamic_vertex_input')
input_controls=[
 ('ready route bypass omitted','pgraph_vk_ready_vertex_input(pg);','(void)pgraph_vk_ready_vertex_input;'),
 ('utility retains input cache','r->vi_last.valid = false;','/* utility cache remains valid */'),
 ('binding payload cached by counts only','!memcmp(r->vi_last.bind, r->vertex_binding_descriptions,\n                nb * sizeof(r->vi_last.bind[0]))','true'),
 ('attribute payload cached by counts only','!memcmp(r->vi_last.attr, r->vertex_attribute_descriptions,\n                na * sizeof(r->vi_last.attr[0]))','true'),
 ('attribute offset lost','.offset = r->vertex_attribute_descriptions[i].offset,','.offset = 0,'),
 ('instance divisor lost','.divisor = 1,','.divisor = 0,'),
]
with tempfile.TemporaryDirectory(prefix='vk-so-input-') as tmp:
    p=Path(tmp);source=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r/'deps/vulkan-headers'),str(source),'-o',str(exe)]
    def run_input(body,negative=False):
        source.write_text(input_template.replace('/* PRODUCTION */',body));subprocess.run(cmd,env=env,check=True)
        result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=60)
        if negative:assert result.returncode!=0 and re.search('assertion|Assertion',result.stderr),(result.returncode,result.stdout,result.stderr)
        else:assert result.returncode==0,(result.returncode,result.stdout,result.stderr);print(result.stdout,end='',flush=True)
    run_input(input_body)
    for label,before,after in input_controls:
        assert before in input_body,label;run_input(input_body.replace(before,after,1),True)
        print('required actual-input control rejected:',label,flush=True)
print(f'{len(input_controls)} actual-input controls PASS',flush=True)
