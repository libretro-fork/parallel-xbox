/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#ifdef _WIN32
#include "ui/gl-dispatch.h"
#include "gloffscreen.h"
extern void libretro_gl_set_standalone_mode(void);
#else
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
/* STRING_HELPERS */
/* TYPES */
#define GEOM_DYNAMIC_VARIANTS 22
/* GENERATORS */
/* BASELINE */
/* The ordinary GL builder also uses indirect inputs when winding differs by
 * primitive parity. llvmpipe miscompiles those reads. Independently lower the
 * reference to constant input loads with ternary selection, and check every
 * emitted color against a CPU oracle below. Original source bytes are compared
 * with the pinned parent before this test-only lowering. This is not renderer
 * code or a source-allocation/latency qualification. */
static MString *lower_reference(MString *original, unsigned count)
{
    MString *text=mstring_from_fmt("%s",original->str);
    const char *names[]={"vtxD0","vtxD1","vtxB0","vtxB1","vtxFog",
                         "vtxT0","vtxT1","vtxT2","vtxT3","vtxPos"};
    char *first=strstr(text->str,"void ");assert(first);
    for(char*p=first;*p;p++){
        for(unsigned k=0;k<ARRAY_SIZE(names);k++){
            char token[32];snprintf(token,sizeof(token),"v_%s",names[k]);
            size_t n=strlen(token);if(strncmp(p,token,n)||p[n]!='[')continue;
            char *end=p+n+1;unsigned nesting=1;
            while(nesting){assert(*end);if(*end=='[')nesting++;if(*end==']')nesting--;if(nesting)end++;}
            p[0]='r';p[n]='(';*end=')';break;
        }
        if(!strncmp(p,"gl_in[",6)){
            char *end=strchr(p+6,']');assert(end);
            const char *field=!strncmp(end+1,".gl_Position",12)?"Position":
                              !strncmp(end+1,".gl_PointSize",13)?"PointSize":NULL;
            assert(field);size_t n=(end-p)+1+4+strlen(field);
            char access[128];int written=snprintf(access,sizeof(access),"r_%s(%.*s)",field,(int)(end-p-6),p+6);
            assert(written>0&&(size_t)written<=n);memset(p,' ',n);memcpy(p,access,written);
        }
    }
    size_t prefix=first-text->str;
    MString *result=mstring_from_fmt("%.*s",(int)prefix,text->str);
    for(unsigned k=0;k<ARRAY_SIZE(names)+2;k++){
        const char *name=k<ARRAY_SIZE(names)?names[k]:k==ARRAY_SIZE(names)?"Position":"PointSize";
        mstring_append_fmt(result,"%s r_%s(int i){return ",k==4||k==ARRAY_SIZE(names)+1?"float":"vec4",name);
        for(unsigned i=0;i<count;i++){
            if(i+1<count)mstring_append_fmt(result,"i==%u?",i);
            if(k<ARRAY_SIZE(names))mstring_append_fmt(result,"v_%s[%u]",k==9?"vtxPos0":name,i);
            else mstring_append_fmt(result,"gl_in[%u].gl_%s",i,name);
            if(i+1<count)mstring_append(result,":");
        }
        mstring_append(result,";}\n");
    }
    mstring_append(result,text->str+prefix);mstring_unref(text);return result;
}
static unsigned reference_inputs;
static void quiet_abort(int sig) { (void)sig; _Exit(1); }
static void init_gl(void)
{
#ifdef _WIN32
    libretro_gl_set_standalone_mode();
    GloContext *context = glo_context_create_4_1();
    assert(context); glo_set_current(context);
#else
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(display != EGL_NO_DISPLAY && eglInitialize(display, NULL, NULL));
    assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
    EGLConfig config; EGLint count;
    assert(eglChooseConfig(display, attrs, &config, 1, &count) && count == 1);
    EGLint ca[] = { EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 1,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
    assert(context != EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
#endif
    printf("Geometry GL device=%s version=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
}
static GLuint program(GLenum type, MString *text)
{
    GLuint shader = glCreateShader(type), p = glCreateProgram();
    MString *lowered=type==GL_GEOMETRY_SHADER&&!strstr(text->str,"uniform ivec4 gsControl")?
                     lower_reference(text,reference_inputs):NULL;
    const char *source = lowered?lowered->str:text->str;
    glShaderSource(shader, 1, &source, NULL); glCompileShader(shader);
    GLint ok; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[8192]; glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "compile: %s\n%s\n", log, source); abort(); }
    glProgramParameteri(p, GL_PROGRAM_SEPARABLE, GL_TRUE);
    glAttachShader(p, shader);
    if (type == GL_GEOMETRY_SHADER) {
        const char *varyings[] = { "vtxD0", "vtxD1", "vtxB0", "vtxB1", "vtxFog",
            "gl_SkipComponents3", "vtxT0", "vtxT1", "vtxT2", "vtxT3",
            "vtxPos0", "vtxPos1", "vtxPos2", "triMZ", "gl_SkipComponents3",
            "gl_Position", "gl_PointSize", "gl_SkipComponents3" };
        glTransformFeedbackVaryings(p, ARRAY_SIZE(varyings), varyings, GL_INTERLEAVED_ATTRIBS);
    }
    glLinkProgram(p); glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[8192]; glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "link: %s\n", log); abort(); }
    glDeleteShader(shader);
    if(lowered)mstring_unref(lowered);
    return p;
}
static MString *vertex_source(bool smooth)
{
    MString *s = mstring_from_fmt("#version 410\nout gl_PerVertex {vec4 gl_Position;float gl_PointSize;};\n");
    pgraph_glsl_get_vtx_header(s, true, smooth, false, true, false);
    mstring_append(s,
        "uniform int inputVertices; uniform int isStrip;\n"
        "void main(){int i=gl_VertexID%inputVertices;int c=gl_VertexID/inputVertices;\n"
        "int corner=inputVertices==4&&isStrip==0?ivec4(0,1,3,2)[i]:i;\n"
        "float x=(corner&1)==0?0.375:8.375;float y=(corner&2)==0?2.625:10.625;\n"
        "float z=(c%8)==1?0.25+float(i)*0.125:0.25;\n"
        "float w=(c%8)==2?1.0+float(i)*0.25:1.0;\n"
        "if((c%8)==3)x=0.375;if((c%8)==4&&i==1)x=uintBitsToFloat(0x7fc00000u);\n"
        "if((c%8)==5&&i==1)y=uintBitsToFloat(0x7f800000u);\n"
        "if((c%8)==6){x+=65536.0;y+=65536.0;}\n"
        "if(c==8){x=0.0;y=0.0;}if(c==9)y=x*0.25;\n"
        "if(c==10)x=uintBitsToFloat(0x80000000u);\n"
        "if(c==11)x=uintBitsToFloat(0x007fffffu+uint(i));\n"
        "if(c==12){x*=1e20;y*=1e-20;w=uintBitsToFloat(0x80000000u);}\n"
        "if(c==13)w=uintBitsToFloat(0x00000001u);\n"
        "if(c==14)z=uintBitsToFloat(0x7fa00001u);if(c==15)w=uintBitsToFloat(0x7f800000u);\n"
        "v_vtxPos0=vec4(x,y,z,w);v_vtxPos1=v_vtxPos0;v_vtxPos2=v_vtxPos0;\n"
        "v_triMZ=0.0;gl_Position=vec4(x*0.0625,y*0.0625,z,w);gl_PointSize=1.0+float(i);\n"
        "v_vtxD0=vec4(float(i),float(c),0.25,1);v_vtxD1=v_vtxD0+vec4(0.125);\n"
        "v_vtxB0=v_vtxD0+vec4(0.25);v_vtxB1=v_vtxD0+vec4(0.5);v_vtxFog=float(i)*0.125;\n"
        "v_vtxT0=vec4(x*0.125,y*0.25,0,1);v_vtxT1=v_vtxT0*vec4(-1,2,1,1);\n"
        "v_vtxT2=v_vtxT0*vec4(2,-1,1,1);v_vtxT3=v_vtxT0*vec4(-2,-2,1,1);\n"
        "if((c%8)==7&&i==1){v_vtxT0.w=2;v_vtxT1.w=2;v_vtxT2.w=2;v_vtxT3.w=2;}\n"
        "}\n");
    return s;
}
enum { PRIMITIVES = 16, RECORD_FLOATS = 60, MAX_FLOATS = PRIMITIVES*8*RECORD_FLOATS,
       GUARD_FLOATS = 16 };
static GLuint query, buffer, pipeline;
static uint32_t captured[2][MAX_FLOATS+GUARD_FLOATS];
static GLenum input_mode(const GeomState *s)
{
    if (s->primitive_mode == PRIM_TYPE_QUADS || s->primitive_mode == PRIM_TYPE_QUAD_STRIP)
        return GL_LINES_ADJACENCY;
    if (s->primitive_mode == PRIM_TYPE_LINES || s->primitive_mode == PRIM_TYPE_LINE_LOOP ||
        s->primitive_mode == PRIM_TYPE_LINE_STRIP ||
        (s->primitive_mode == PRIM_TYPE_POLYGON && s->polygon_front_mode == POLY_MODE_LINE))
        return GL_LINES;
    return GL_TRIANGLES;
}
static GLenum output_mode(const GeomState *s)
{
    if (input_mode(s) == GL_LINES)
        return s->wide_lines && s->primitive_mode != PRIM_TYPE_POLYGON ? GL_TRIANGLES : GL_LINES;
    return s->polygon_front_mode == POLY_MODE_FILL ? GL_TRIANGLES :
           s->polygon_front_mode == POLY_MODE_POINT ? GL_POINTS : GL_LINES;
}
static unsigned capture(GLuint vs, GLuint gs, const GeomState *s, unsigned path)
{
    unsigned vertices = input_mode(s) == GL_LINES ? 2 : input_mode(s) == GL_TRIANGLES ? 3 : 4;
    GLenum out = output_mode(s);
    glProgramUniform1i(vs, glGetUniformLocation(vs, "inputVertices"), vertices);
    glProgramUniform1i(vs, glGetUniformLocation(vs, "isStrip"), s->primitive_mode == PRIM_TYPE_QUAD_STRIP);
    glProgramUniform2f(gs, glGetUniformLocation(gs, "gsSurfaceSize"), 64, 64);
    glProgramUniform1f(gs, glGetUniformLocation(gs, "gsLineWidth"), 3);
    glProgramUniform1i(gs, glGetUniformLocation(gs, "gsKeepWinding"), 2);
    glUseProgramStages(pipeline, GL_VERTEX_SHADER_BIT, vs);
    glUseProgramStages(pipeline, GL_GEOMETRY_SHADER_BIT, gs);
    for (unsigned i=0;i<MAX_FLOATS+GUARD_FLOATS;i++) captured[path][i]=0x7fc12345;
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer);
    glBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, sizeof(captured[path]), captured[path]);
    glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query);
    glBeginTransformFeedback(out);
    glDrawArrays(input_mode(s), 0, vertices*PRIMITIVES);
    glEndTransformFeedback(); glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
    unsigned written; glGetQueryObjectuiv(query, GL_QUERY_RESULT, &written);
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, sizeof(captured[path]), captured[path]);
    assert(glGetError() == GL_NO_ERROR);
    unsigned words=written*(out==GL_TRIANGLES?3:out==GL_LINES?2:1)*RECORD_FLOATS;
    assert(words<=MAX_FLOATS);
    for (unsigned i=words;i<MAX_FLOATS+GUARD_FLOATS;i++) assert(captured[path][i]==0x7fc12345);
    return words;
}
static void compare(unsigned words, unsigned lane, unsigned test)
{
    for (unsigned i=0;i<words;i++) {
        /* Skipped TF components remain sentinels; NaN payload choice is not
         * specified. All finite values, infinities and signed zeros are exact. */
        if (captured[0][i] == captured[1][i]) continue;
        float a,b;memcpy(&a,&captured[0][i],4);memcpy(&b,&captured[1][i],4);
        if (isnan(a)&&isnan(b)) continue;
        fprintf(stderr,"difference lane=%u test=%u scalar=%u: %08x != %08x\n",
                lane,test,i,captured[0][i],captured[1][i]);abort();
    }
}
static void check_colors(unsigned words, const GeomState *s, unsigned path)
{
    const unsigned order[3][3]={{0,1,2},{2,0,1},{1,2,0}};
    for(unsigned at=0;at<words;at+=RECORD_FLOATS){
        float x,y,point;memcpy(&x,&captured[path][at],4);memcpy(&y,&captured[path][at+1],4);
        memcpy(&point,&captured[path][at+56],4);
        assert(y>=0&&y<PRIMITIVES&&y==(float)(unsigned)y);
        unsigned primitive=(unsigned)y,expected;
        if(s->smooth_shading){assert(point>=1&&point<=4);expected=(unsigned)point-1;}
        else if(s->primitive_mode==PRIM_TYPE_QUADS||s->primitive_mode==PRIM_TYPE_QUAD_STRIP)expected=3;
        else if(input_mode(s)==GL_LINES)expected=s->primitive_mode==PRIM_TYPE_POLYGON?0:s->first_vertex_is_provoking?0:1;
        else{
            unsigned slot=s->primitive_mode==PRIM_TYPE_POLYGON?2:s->first_vertex_is_provoking?0:
                          s->primitive_mode==PRIM_TYPE_TRIANGLE_STRIP?2-(primitive&1):
                          s->primitive_mode==PRIM_TYPE_TRIANGLE_FAN?1:2;
            expected=order[(primitive&1)?s->tri_rot1:s->tri_rot0][slot];
        }
        if(x!=(float)expected){fprintf(stderr,"CPU color oracle path=%u primitive=%u: %.9g != %u\n",path,primitive,x,expected);abort();}
    }
}
int main(void)
{
    signal(SIGABRT, quiet_abort);setvbuf(stdout,NULL,_IONBF,0);init_gl();
    GLuint fb, rb;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,1,1);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenProgramPipelines(1,&pipeline);glBindProgramPipeline(pipeline);
    glGenBuffers(1,&buffer);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,sizeof(captured[0]),NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);glGenQueries(1,&query);
    glEnable(GL_RASTERIZER_DISCARD);
    GLuint vs[2], ready[GEOM_DYNAMIC_VARIANTS];MString *sources[GEOM_DYNAMIC_VARIANTS];
    for(unsigned i=0;i<2;i++){MString*s=vertex_source(i);vs[i]=program(GL_VERTEX_SHADER,s);mstring_unref(s);}
    for(unsigned i=0;i<GEOM_DYNAMIC_VARIANTS;i++){
        GeomState s;GenGeomGlslOptions opts;pgraph_glsl_geom_dynamic_state(i,&s,&opts);
        assert(pgraph_glsl_geom_dynamic_variant(&s,opts)==(int)i);
        sources[i]=pgraph_glsl_gen_geom_dynamic(&s,opts);ready[i]=program(GL_GEOMETRY_SHADER,sources[i]);
    }
    unsigned tests=0;uint64_t compared=0;
    for(unsigned primitive=PRIM_TYPE_LINES;primitive<=PRIM_TYPE_POLYGON;primitive++)
    for(unsigned mode=POLY_MODE_FILL;mode<=POLY_MODE_LINE;mode++)
    for(unsigned smooth=0;smooth<2;smooth++)for(unsigned wide=0;wide<2;wide++)
    for(unsigned ui=0;ui<=4;ui++)for(unsigned control=0;control<36;control++){
        GeomState s={.primitive_mode=primitive,.polygon_front_mode=mode,.polygon_back_mode=mode,
            .smooth_shading=smooth,.wide_lines=wide,.z_perspective=control&1,
            .first_vertex_is_provoking=!!(control&2),.tri_rot0=(control/4)%3,.tri_rot1=(control/12)%3};
        GenGeomGlslOptions opts={.locations=true,.ui_clamp_stage=ui};
        int lane=pgraph_glsl_geom_dynamic_variant(&s,opts);if(lane<0)continue;
        MString *dynamic=pgraph_glsl_gen_geom_dynamic(&s,opts);
        assert(!strcmp(dynamic->str,sources[lane]->str));mstring_unref(dynamic);
        MString *reference=pgraph_glsl_gen_geom(&s,opts),*baseline=baseline_gen_geom(&s,opts);
        assert(!strcmp(reference->str,baseline->str));mstring_unref(baseline);
        for(unsigned target=0;target<2;target++){
            GenGeomGlslOptions other=opts;other.vulkan=target;other.locations=false;
            MString *a=pgraph_glsl_gen_geom(&s,other),*b=baseline_gen_geom(&s,other);
            assert(!strcmp(a->str,b->str));mstring_unref(a);mstring_unref(b);
        }
        reference_inputs=input_mode(&s)==GL_LINES?2:input_mode(&s)==GL_TRIANGLES?3:4;
        GLuint exact=program(GL_GEOMETRY_SHADER,reference);mstring_unref(reference);
        unsigned expected=capture(vs[smooth],exact,&s,0);
        int controls[4];pgraph_glsl_geom_dynamic_controls(&s,ui,controls);
        controls[0] |= (int)UINT32_C(0xa5a50000); /* Reserved bits cannot affect guest controls. */
        /* MUTATE_CONTROLS */
        glUseProgramStages(pipeline,GL_GEOMETRY_SHADER_BIT,ready[lane]);
        glProgramUniform4iv(ready[lane],glGetUniformLocation(ready[lane],"gsControl"),1,controls);
        unsigned actual=capture(vs[smooth],ready[lane],&s,1);assert(actual==expected);
        check_colors(expected,&s,0);check_colors(actual,&s,1);
        compare(expected,lane,tests);compared+=expected;tests++;glDeleteProgram(exact);
    }
    for(unsigned i=0;i<GEOM_DYNAMIC_VARIANTS;i++){glDeleteProgram(ready[i]);mstring_unref(sources[i]);}
    glDeleteProgram(vs[0]);glDeleteProgram(vs[1]);glDeleteProgramPipelines(1,&pipeline);
    glDeleteQueries(1,&query);glDeleteBuffers(1,&buffer);glDeleteVertexArrays(1,&vao);
    printf("PASS geometry: %u cases, %llu scalar checks, 22 invariant startup stages; ordinary sources byte-exact\n",
           tests,(unsigned long long)compared);
    return 0;
}
