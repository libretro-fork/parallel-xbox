/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The extracted helpers use captured API calls, not a graphics driver. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct ShaderModuleInfo { VkShaderEXT shader_object; int uses; } ShaderModuleInfo;
typedef struct ShaderBinding { struct {ShaderModuleInfo *module_info;} vsh,geom,psh; struct{struct{unsigned polygon_front_mode;}geom;}state; bool shader_object_route;}ShaderBinding;
typedef struct Surface {VkImageView image_view;struct{VkImageAspectFlags aspect;}host_fmt;}Surface;
typedef struct PGRAPHVkState {
 VkCommandBuffer command_buffer;ShaderBinding *shader_binding;Surface *color_binding,*zeta_binding;
 struct {bool enabled,draw,bound,rendering;VkPhysicalDeviceFeatures features;}shader_objects;
 struct{bool valid;}dyn_last,vi_last;
 bool in_render_pass,query_in_flight,query_in_render_pass,cost_recording,shader_bindings_changed;
} PGRAPHVkState;
typedef struct PGRAPHState {
 PGRAPHVkState *vk_renderer_state;struct{unsigned width,height;}surface_binding_dim;
 struct{unsigned clip_x,clip_y,clip_width,clip_height;}surface_shape;
 float line_width,surface_scale_factor;unsigned aa_x,aa_y;bool wide;
}PGRAPHState;
static unsigned serial,query_end,cost_end,interrupt_at,pass_end,barrier_at,begin_at,bind_at,emit_at,vertex_at,descriptor_at,push_at;
static VkViewport viewport;static VkRect2D scissor;static VkRenderingInfo rendering;
static VkRenderingAttachmentInfo color,depth,stencil;static VkMemoryBarrier dependency;
static VkPipelineStageFlags source_stage,dest_stage;
static unsigned bind_count,last_n;static VkShaderStageFlagBits last_stages[5];static VkShaderEXT last_objects[5];
static unsigned state_count;static uint64_t states;static bool line_wide,expected_force;
static unsigned viewport_calls,scissor_calls;
/* POLYGON_MAP */
static void pgraph_apply_scaling_factor(PGRAPHState*p,unsigned*x,unsigned*y){*x*=p->surface_scale_factor;*y*=p->surface_scale_factor;}
static void pgraph_apply_anti_aliasing_factor(PGRAPHState*p,unsigned*x,unsigned*y){*x*=p->aa_x;*y*=p->aa_y;}
static bool dynamic_line_width(PGRAPHVkState*r,const void*s){(void)r;(void)s;return line_wide;}
static float clamp_line_width_to_device_limits(PGRAPHState*p,float w){(void)p;return w>8?8:w;}
static void pgraph_vk_shader_object_use(PGRAPHVkState*r,ShaderModuleInfo*m){(void)r;m->uses++;}
static void vkCmdBindShadersEXT(VkCommandBuffer c,uint32_t n,const VkShaderStageFlagBits*s,const VkShaderEXT*o){(void)c;assert(n<=5);last_n=n;memcpy(last_stages,s,n*sizeof(*s));memcpy(last_objects,o,n*sizeof(*o));bind_at=++serial;bind_count++;}
static void vkCmdPipelineBarrier(VkCommandBuffer c,VkPipelineStageFlags s,VkPipelineStageFlags d,VkDependencyFlags f,uint32_t n,const VkMemoryBarrier*m,uint32_t nb,const VkBufferMemoryBarrier*b,uint32_t ni,const VkImageMemoryBarrier*i){(void)c;(void)f;(void)b;(void)i;assert(n==1&&!nb&&!ni);dependency=*m;source_stage=s;dest_stage=d;barrier_at=++serial;}
static void vkCmdBeginRendering(VkCommandBuffer c,const VkRenderingInfo*i){(void)c;rendering=*i;if(i->pColorAttachments){color=*i->pColorAttachments;rendering.pColorAttachments=&color;}if(i->pDepthAttachment){depth=*i->pDepthAttachment;rendering.pDepthAttachment=&depth;}if(i->pStencilAttachment){stencil=*i->pStencilAttachment;rendering.pStencilAttachment=&stencil;}begin_at=++serial;}
static void vkCmdSetViewportWithCountEXT(VkCommandBuffer c,uint32_t n,const VkViewport*v){(void)c;assert(n==1);viewport=*v;viewport_calls++;}
static void vkCmdSetScissorWithCountEXT(VkCommandBuffer c,uint32_t n,const VkRect2D*v){(void)c;assert(n==1);scissor=*v;scissor_calls++;}
#define CAPTURE(name,bit,type,expected) static void name(VkCommandBuffer c,type value){(void)c;assert(value==(expected));states|=1ull<<(bit);state_count++;}
CAPTURE(vkCmdSetRasterizerDiscardEnableEXT,0,VkBool32,VK_FALSE)
CAPTURE(vkCmdSetPrimitiveRestartEnableEXT,1,VkBool32,VK_FALSE)
static unsigned expected_polygon;
static void vkCmdSetPolygonModeEXT(VkCommandBuffer c,VkPolygonMode v){(void)c;assert(v==pgraph_polygon_mode_vk_map[expected_polygon]);states|=1ull<<2;state_count++;}
CAPTURE(vkCmdSetRasterizationSamplesEXT,3,VkSampleCountFlagBits,VK_SAMPLE_COUNT_1_BIT)
static void vkCmdSetSampleMaskEXT(VkCommandBuffer c,VkSampleCountFlagBits n,const VkSampleMask*m){(void)c;assert(n==VK_SAMPLE_COUNT_1_BIT&&*m==~0u);states|=1ull<<4;state_count++;}
CAPTURE(vkCmdSetDepthBiasEnableEXT,5,VkBool32,VK_FALSE)
static void vkCmdSetDepthBias(VkCommandBuffer c,float a,float b,float d){(void)c;assert(a==0&&b==0&&d==0);states|=1ull<<6;state_count++;}
CAPTURE(vkCmdSetAlphaToCoverageEnableEXT,7,VkBool32,VK_FALSE)
CAPTURE(vkCmdSetDepthClampEnableEXT,8,VkBool32,VK_TRUE)
static void vkCmdSetDepthBounds(VkCommandBuffer c,float a,float b){(void)c;assert(a==0&&b==1);states|=1ull<<9;state_count++;}
CAPTURE(vkCmdSetAlphaToOneEnableEXT,10,VkBool32,VK_FALSE)
CAPTURE(vkCmdSetLogicOpEnableEXT,11,VkBool32,VK_FALSE)
CAPTURE(vkCmdSetLogicOpEXT,12,VkLogicOp,VK_LOGIC_OP_COPY)
static float expected_line;
static void vkCmdSetLineWidth(VkCommandBuffer c,float w){(void)c;assert(w==expected_line);states|=1ull<<13;state_count++;}
static void emit_dynamic_state(PGRAPHState*p,bool force){assert(force==expected_force&&p->vk_renderer_state->shader_objects.bound);p->vk_renderer_state->dyn_last.valid=true;emit_at=++serial;}
static void emit_dynamic_vertex_input(PGRAPHState*p){assert(p->vk_renderer_state->shader_objects.bound);vertex_at=++serial;}
static void bind_descriptor_sets(PGRAPHState*p){(void)p;descriptor_at=++serial;}
static void push_vertex_attr_values(PGRAPHState*p){(void)p;push_at=++serial;}
static void end_query(PGRAPHVkState*r){assert(r->in_render_pass);r->query_in_flight=false;query_end=++serial;}
static void vk_cost_close_recorded_queries(PGRAPHVkState*r){assert(r->in_render_pass);r->cost_recording=false;cost_end=++serial;}
static void xemu_cost_backend_interrupt(void){interrupt_at=++serial;}
static void vkCmdEndRendering(VkCommandBuffer c){(void)c;pass_end=++serial;}
static void vkCmdEndRenderPass(VkCommandBuffer c){(void)c;pass_end=++serial;}
/* PRODUCTION */
static void reset(void){serial=query_end=cost_end=interrupt_at=pass_end=barrier_at=begin_at=bind_at=emit_at=vertex_at=descriptor_at=push_at=0;states=0;state_count=0;}
int main(void){
 unsigned cases=0;
 for(unsigned attachments=1;attachments<8;attachments++)for(unsigned features=0;features<64;features++)for(unsigned aa=0;aa<3;aa++)for(unsigned scale=1;scale<=3;scale++)for(unsigned polygon=0;polygon<3;polygon++){
  reset();Surface c={.image_view=(VkImageView)(uintptr_t)31},z={.image_view=(VkImageView)(uintptr_t)32,.host_fmt.aspect=((attachments&2)?VK_IMAGE_ASPECT_DEPTH_BIT:0)|((attachments&4)?VK_IMAGE_ASPECT_STENCIL_BIT:0)};
  ShaderModuleInfo v={.shader_object=(VkShaderEXT)(uintptr_t)41},g={.shader_object=(VkShaderEXT)(uintptr_t)42},f={.shader_object=(VkShaderEXT)(uintptr_t)43};
  ShaderBinding b={.vsh.module_info=&v,.psh.module_info=&f,.shader_object_route=true};b.state.geom.polygon_front_mode=polygon;
  PGRAPHVkState r={.shader_binding=&b,.color_binding=(attachments&1)?&c:NULL,.zeta_binding=(attachments&6)?&z:NULL,.shader_objects={.enabled=true,.draw=true}};
  VkPhysicalDeviceFeatures*cap=&r.shader_objects.features;cap->geometryShader=features&1;cap->tessellationShader=!!(features&2);cap->depthClamp=!!(features&4);cap->depthBounds=!!(features&8);cap->alphaToOne=!!(features&16);cap->logicOp=!!(features&32);
  PGRAPHState p={.vk_renderer_state=&r,.surface_binding_dim={32,24},.surface_shape={1,2,7,5},.surface_scale_factor=scale,.line_width=16,.aa_x=aa?2:1,.aa_y=aa==2?2:1};
  shader_objects_begin_rendering(&p,32*scale,24*scale);
  assert(barrier_at&&begin_at>barrier_at&&r.in_render_pass&&r.shader_objects.rendering);
  VkAccessFlags access=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  VkPipelineStageFlags stages=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  assert(dependency.srcAccessMask==access&&dependency.dstAccessMask==access&&source_stage==stages&&dest_stage==stages);
  assert(rendering.colorAttachmentCount==!!(attachments&1)&&!!rendering.pColorAttachments==!!(attachments&1)&&!!rendering.pDepthAttachment==!!(attachments&2)&&!!rendering.pStencilAttachment==!!(attachments&4));
  assert(rendering.layerCount==1&&rendering.renderArea.extent.width==32*scale&&rendering.renderArea.extent.height==24*scale);
  if(rendering.pColorAttachments)assert(color.imageView==c.image_view&&color.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD&&color.storeOp==VK_ATTACHMENT_STORE_OP_STORE&&color.imageLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  if(rendering.pDepthAttachment)assert(depth.imageView==z.image_view&&depth.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD&&depth.storeOp==VK_ATTACHMENT_STORE_OP_STORE&&depth.imageLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  if(rendering.pStencilAttachment)assert(stencil.imageView==z.image_view&&stencil.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD&&stencil.storeOp==VK_ATTACHMENT_STORE_OP_STORE&&stencil.imageLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  uint64_t complete=255|(1ull<<13);if(cap->depthClamp)complete|=1ull<<8;if(cap->depthBounds)complete|=1ull<<9;if(cap->alphaToOne)complete|=1ull<<10;if(cap->logicOp)complete|=3ull<<11;
  for(unsigned cycle=0;cycle<2;cycle++){
   r.shader_bindings_changed=true; /* Actual GS -> NULL-GS binding transition. */
   b.geom.module_info=cap->geometryShader&&!cycle?&g:NULL;line_wide=cycle;expected_line=cycle?2*scale:1;expected_polygon=polygon;states=0;
   expected_force=cycle==0;unsigned views=viewport_calls,clips=scissor_calls;
   shader_objects_bind_draw(&p);
   assert(viewport_calls==views+1&&scissor_calls==clips+1);
   unsigned n=0;assert(last_stages[n]==VK_SHADER_STAGE_VERTEX_BIT&&last_objects[n++]==v.shader_object);
   if(cap->tessellationShader){assert(last_stages[n]==VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT&&!last_objects[n]);n++;assert(last_stages[n]==VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT&&!last_objects[n]);n++;}
   if(cap->geometryShader){assert(last_stages[n]==VK_SHADER_STAGE_GEOMETRY_BIT&&last_objects[n]==(cycle?VK_NULL_HANDLE:g.shader_object));n++;}
   assert(last_stages[n]==VK_SHADER_STAGE_FRAGMENT_BIT&&last_objects[n++]==f.shader_object&&n==last_n);
   assert(v.uses==(int)cycle+1&&f.uses==(int)cycle+1&&g.uses==!!cap->geometryShader);
   assert(viewport.width==32*scale&&viewport.height==24*scale&&viewport.minDepth==0&&viewport.maxDepth==1);
   assert(scissor.offset.x==(int)(p.aa_x*scale)&&scissor.offset.y==(int)(2*p.aa_y*scale)&&scissor.extent.width==7*p.aa_x*scale&&scissor.extent.height==5*p.aa_y*scale);
   assert(states==(cycle==0?complete:(1ull<<2|1ull<<13)));
   assert(bind_at<emit_at&&emit_at<vertex_at&&vertex_at<descriptor_at&&descriptor_at<push_at);
  }
  /* An unchanged tuple in the same recording already retains its submission.
   * Uniform/descriptor/input work must still run on the repeated draw. */
  r.shader_bindings_changed=false;expected_force=false;reset();
  unsigned cached_binds=bind_count,cached_uses=v.uses,cached_views=viewport_calls;
  shader_objects_bind_draw(&p);
  assert(bind_count==cached_binds&&v.uses==(int)cached_uses&&!bind_at);
  assert(viewport_calls==cached_views+1&&states==(1ull<<2|1ull<<13));
  assert(emit_at<vertex_at&&vertex_at<descriptor_at&&descriptor_at<push_at);
  r.dyn_last.valid=r.vi_last.valid=true;shader_objects_unbind(&r);assert(!r.shader_objects.bound&&!r.dyn_last.valid&&!r.vi_last.valid);
  for(unsigned i=0;i<last_n;i++){assert(!last_objects[i]);}
  unsigned calls=bind_count;shader_objects_unbind(&r);assert(bind_count==calls);
  expected_force=true;states=0;shader_objects_bind_draw(&p);assert(states==complete);
  /* Same state is inherited only while BOTH owners remain valid. Changing
   * polygon/line/viewport/scissor must still update every SO draw. */
  for(unsigned owners=0;owners<4;owners++){
   r.shader_bindings_changed=true; /* The fixture changes the raw polygon key. */
   r.shader_objects.bound=owners&1;r.dyn_last.valid=owners&2;
   p.surface_binding_dim.width=33+owners;p.surface_shape.clip_x=owners+2;
   b.state.geom.polygon_front_mode=(polygon+owners)%3;expected_polygon=b.state.geom.polygon_front_mode;
   p.line_width=(owners+1)*8;expected_line=(owners+1)*scale;if(expected_line>8)expected_line=8;
   expected_force=owners!=3;states=0;unsigned views=viewport_calls,clips=scissor_calls,used=v.uses;
   shader_objects_bind_draw(&p);
   assert(v.uses==(int)used+1&&viewport_calls==views+1&&scissor_calls==clips+1);
   assert(states==(expected_force?complete:(1ull<<2|1ull<<13)));
   assert(viewport.width==(33+owners)*scale&&scissor.offset.x==(int)((owners+2)*p.aa_x*scale));
   assert(bind_at<emit_at&&emit_at<vertex_at&&vertex_at<descriptor_at&&descriptor_at<push_at);
  }
  /* Execute the actual new-command invalidation boundary even if the old
   * recording ended with shader objects bound and valid dynamic snapshots. */
  r.shader_bindings_changed=false;
  r.shader_objects.bound=true;r.dyn_last.valid=r.vi_last.valid=true;
  new_command_state_boundary(&r);assert(!r.dyn_last.valid&&!r.vi_last.valid);
  expected_force=true;states=0;unsigned previous_uses=v.uses;
  shader_objects_bind_draw(&p);assert(states==complete&&v.uses==(int)previous_uses+1);
  r.query_in_flight=r.query_in_render_pass=r.cost_recording=true;end_render_pass(&r);assert(query_end<cost_end&&cost_end<interrupt_at&&interrupt_at<pass_end&&!r.in_render_pass&&!r.shader_objects.rendering);cases++;
 }
 for(unsigned flags=0;flags<32;flags++){
  reset();PGRAPHVkState r={.in_render_pass=!!(flags&1),.query_in_flight=!!(flags&2),.query_in_render_pass=!!(flags&4),.cost_recording=!!(flags&8),.shader_objects.rendering=!!(flags&16)};
  bool active=r.in_render_pass;end_render_pass(&r);
  assert(!!pass_end==active&&!!query_end==(active&&!!(flags&2)&&!!(flags&4))&&!!cost_end==(active&&!!(flags&8)));
  if(query_end)assert(query_end<pass_end);
  if(cost_end)assert(cost_end<interrupt_at&&interrupt_at<pass_end);
  assert(!r.in_render_pass&&r.shader_objects.rendering==(!active&&!!(flags&16)));
  unsigned previous=serial;end_render_pass(&r);assert(serial==previous);
 }
 printf("Shader-object state: %u attachment/feature/AA/scale/polygon cases, two GS/null cycles plus four inherited/invalidation owner schedules and actual new-command boundary each, 32 classic/dynamic/query/cost end schedules; captured native API arguments PASS\n",cases);return 0;
}
