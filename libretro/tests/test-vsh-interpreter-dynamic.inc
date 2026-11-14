/* SPDX-License-Identifier: GPL-2.0-or-later */
static uint32_t control_flags(const VshState *s)
{
    return pgraph_glsl_vsh_interpreter_flags(s);
}
static void set_control_flags(VshState *s,uint32_t v)
{
    s->point_params_enable=(v&1u)!=0;s->specular_enable=(v&2u)!=0;
    s->ignore_specular_alpha=(v&4u)!=0;s->fog_enable=(v&8u)!=0;
    s->fog_dynamic=(v&16u)!=0;s->fog_mode=(enum VshFogMode)((v>>5)&7u);
}
static void metadata_case(unsigned test,VshState *s,SpvVshOptions *opts,
                          unsigned *variant,unsigned *m,unsigned *l)
{
    if(test<42512)return;
    unsigned f=test-42512,pattern=f%192;
    const enum VshFogMode modes[]={FOG_MODE_LINEAR,FOG_MODE_EXP,FOG_MODE_EXP2,
        FOG_MODE_LINEAR_ABS,FOG_MODE_EXP_ABS,FOG_MODE_EXP2_ABS};
    *s=(VshState){.smooth_shading=f>=192,.point_params_enable=!!(pattern&1),
        .specular_enable=!!(pattern&2),.ignore_specular_alpha=!!(pattern&4),
        .fog_enable=!!(pattern&8),.fog_dynamic=!!(pattern&16),.fog_mode=modes[pattern/32]};
    s->programmable.program_length=6;*variant=0;*m=MAC_MOV;*l=ILU_NOP;
    token(s->programmable.program_data[0],MAC_MOV,ILU_NOP,0,0,false,false);
    token(s->programmable.program_data[1],MAC_MOV,ILU_NOP,1,4,false,false);
    token(s->programmable.program_data[2],MAC_MOV,ILU_NOP,2,8,false,false);
    token(s->programmable.program_data[3],MAC_MOV,ILU_NOP,3,6,false,false);
    token(s->programmable.program_data[4],MAC_MOV,ILU_NOP,4,5,false,false);
    token(s->programmable.program_data[5],MAC_MOV,ILU_NOP,9,9,false,true);
    *opts=(SpvVshOptions){.preserve_float32=true};
    /* Independent protocol oracle: unrelated high bits are never encoded. */
    assert(control_flags(s)==((pattern%32)|((uint32_t)modes[pattern/32]<<5)));
}
