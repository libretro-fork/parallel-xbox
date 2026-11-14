/*
 * Geforce NV2A PGRAPH GLSL Shader Generator
 *
 * Copyright (c) 2015 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2020-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "geom.h"

/* wide_lines took the padding byte: the shader state size, and with it the
 * seed files and dictionary, must not move. */
QEMU_BUILD_BUG_ON(sizeof(GeomState) != 20);

void pgraph_glsl_set_geom_state(PGRAPHState *pg, GeomState *state)
{
    state->primitive_mode = (enum ShaderPrimitiveMode)pg->primitive_mode;

    state->polygon_front_mode = (enum ShaderPolygonMode)GET_MASK(
        pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER),
        NV_PGRAPH_SETUPRASTER_FRONTFACEMODE);
    state->polygon_back_mode = (enum ShaderPolygonMode)GET_MASK(
        pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER),
        NV_PGRAPH_SETUPRASTER_BACKFACEMODE);

    state->smooth_shading = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_3),
                                     NV_PGRAPH_CONTROL_3_SHADEMODE) ==
                            NV_PGRAPH_CONTROL_3_SHADEMODE_SMOOTH;

    state->first_vertex_is_provoking =
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_3),
                 NV_PGRAPH_CONTROL_3_PROVOKING_VERTEX) ==
        NV_PGRAPH_CONTROL_3_PROVOKING_VERTEX_FIRST;

    state->z_perspective = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0) &
                           NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE;

    if (pgraph_renderer(pg)->ops.get_gpu_properties) {
        GPUProperties *gpu_props = pgraph_renderer(pg)->ops.get_gpu_properties();

        switch (state->primitive_mode) {
        case PRIM_TYPE_TRIANGLES:
            state->tri_rot0 = gpu_props->geom_shader_winding.tri;
            state->tri_rot1 = state->tri_rot0;
            break;
        case PRIM_TYPE_TRIANGLE_STRIP:
            state->tri_rot0 = gpu_props->geom_shader_winding.tri_strip0;
            state->tri_rot1 = gpu_props->geom_shader_winding.tri_strip1;
            break;
        case PRIM_TYPE_TRIANGLE_FAN:
        case PRIM_TYPE_POLYGON:
            state->tri_rot0 = gpu_props->geom_shader_winding.tri_fan;
            state->tri_rot1 = state->tri_rot0;
            break;
        default:
            break;
        }
    }
}

static const char *get_vertex_order(int rot)
{
    if (rot == 0) {
        return "ivec3(0, 1, 2)";
    } else if (rot == 1) {
        return "ivec3(2, 0, 1)";
    } else {
        return "ivec3(1, 2, 0)";
    }
}

bool pgraph_glsl_need_geom(const GeomState *state)
{
    /* FIXME: Missing support for 2-sided-poly mode */
    assert(state->polygon_front_mode == state->polygon_back_mode);
    enum ShaderPolygonMode polygon_mode = state->polygon_front_mode;

    switch (state->primitive_mode) {
    case PRIM_TYPE_POINTS:
        return false;
    case PRIM_TYPE_LINES:
    case PRIM_TYPE_LINE_LOOP:
    case PRIM_TYPE_LINE_STRIP:
    case PRIM_TYPE_TRIANGLES:
    case PRIM_TYPE_TRIANGLE_STRIP:
    case PRIM_TYPE_TRIANGLE_FAN:
    case PRIM_TYPE_QUADS:
    case PRIM_TYPE_QUAD_STRIP:
        return true;
    case PRIM_TYPE_POLYGON:
        if (polygon_mode == POLY_MODE_POINT) {
            assert(!"POLY_MODE_POINT not supported");
            return false;
        }
        return true;
    default:
        return false;
    }
}

/* The 3/4 input positions are already on the GPU. Emit constant indices so
 * the small UI classifier has no shader loops or dynamic attribute indexing. */
static void append_ui_prepare_impl(MString *out, unsigned stage, unsigned count,
                                    bool dynamic)
{
    mstring_append(out,
        "bool uiValid;\nvec2 uiUVLo,uiUVHi;\nvoid ui_prepare(){\n");
    for (unsigned i = 0; i < count; i++) {
        if (dynamic) {
            mstring_append_fmt(out,
                "vec4 uiP%u=v_vtxPos[%u],uiT%u=gsUITex(%u);\n", i,i,i,i);
        } else {
        mstring_append_fmt(out,
            "vec4 uiP%u=v_vtxPos[%u],uiT%u=v_vtxT%u[%u];\n",
            i, i, i, stage, i);
        }
    }
    mstring_append(out,
        "vec2 lo=uiP0.xy,hi=lo;\n"
        "uiValid=uiT0.w>0.0;\n");
    for (unsigned i = 0; i < count; i++) {
        mstring_append_fmt(out,
            "lo=min(lo,uiP%u.xy);hi=max(hi,uiP%u.xy);\n"
            "uiValid=uiValid&&all(equal(uiP%u.zw,uiP0.zw))&&uiT%u.w==uiT0.w;\n"
            "uiValid=uiValid&&!any(isnan(uiP%u))&&!any(isinf(uiP%u))"
            "&&!any(isnan(uiT%u))&&!any(isinf(uiT%u));\n",
            i,i,i,i,i,i,i,i);
    }
    mstring_append(out,
        "uiValid=uiValid&&all(greaterThanEqual(hi-lo,vec2(1.0)))&&!any(isinf(hi-lo));\n");
    for (unsigned i = 0; i < count; i++) {
        mstring_append_fmt(out,
            "uiValid=uiValid&&(uiP%u.x==lo.x||uiP%u.x==hi.x)"
            "&&(uiP%u.y==lo.y||uiP%u.y==hi.y);\n",i,i,i,i);
    }
    mstring_append(out,
        "if(!uiValid)return;\nvec2 dx=vec2(0.0),dy=vec2(0.0);\n"
        "bool foundX=false,foundY=false;\n");
    for (unsigned i = 0; i < count; i++) {
        for (unsigned j = i + 1; j < count; j++) {
            mstring_append_fmt(out,
                "{vec2 d=uiP%u.xy-uiP%u.xy,t=uiT%u.xy-uiT%u.xy;\n"
                "if(!foundX&&d.y==0.0&&abs(d.x)>=1.0){dx=t/d.x;foundX=true;}\n"
                "if(!foundY&&d.x==0.0&&abs(d.y)>=1.0){dy=t/d.y;foundY=true;}}\n",
                j,i,j,i);
        }
    }
    mstring_append(out,
        "uiValid=uiValid&&foundX&&foundY;\n"
        "uiValid=uiValid&&((dx.y==0.0&&dy.x==0.0)||(dx.x==0.0&&dy.y==0.0));\n"
        "uiValid=uiValid&&!any(isnan(dx))&&!any(isinf(dx))"
        "&&!any(isnan(dy))&&!any(isinf(dy));\n");
    if (count == 4) {
        /* Only the opposite corner is independent of the two edge gradients. */
        for (unsigned i = 1; i < count; i++) {
            mstring_append_fmt(out,
                "if(uiP%u.x!=uiP0.x&&uiP%u.y!=uiP0.y){\n"
                "vec2 expected=uiT0.xy+dx*(uiP%u.x-uiP0.x)+dy*(uiP%u.y-uiP0.y);\n"
                "uiValid=uiValid&&all(lessThanEqual(abs(expected-uiT%u.xy),"
                "1e-6*max(vec2(1.0),abs(uiT%u.xy))));}\n",i,i,i,i,i,i);
        }
    }
    /* Preserve the actual native sample range, including fractional origins. */
    mstring_append(out,
        "vec2 first=ceil(lo-vec2(0.5))+vec2(0.5);\n"
        "vec2 last=ceil(hi-vec2(0.5))-vec2(0.5);\n"
        "vec2 a=uiT0.xy+dx*(first.x-uiP0.x)+dy*(first.y-uiP0.y);\n"
        "vec2 b=uiT0.xy+dx*(last.x-uiP0.x)+dy*(last.y-uiP0.y);\n"
        "uiValid=uiValid&&!any(isnan(a))&&!any(isinf(a))"
        "&&!any(isnan(b))&&!any(isinf(b));\n"
        "uiUVLo=min(a,b);uiUVHi=max(a,b);\n"
        "uiValid=uiValid&&all(lessThanEqual(first,last));\n}\n");
}

static void append_ui_prepare(MString *out, unsigned stage, unsigned count)
{
    append_ui_prepare_impl(out, stage, count, false);
}

static void append_ui_prepare_dynamic(MString *out, unsigned count)
{
    append_ui_prepare_impl(out, 0, count, true);
}

/* This output is private to the builder until return. End all appends before
 * the mutable lend; equal-length token substitutions preserve length/capacity.
 * Helper definitions precede begin, so their constant GS loads stay direct. */
static void geom_rewrite_inputs(MString *output, size_t begin)
{
    const char *names[] = { "v_vtxD0", "v_vtxD1", "v_vtxB0", "v_vtxB1",
                            "v_vtxFog", "v_vtxT0", "v_vtxT1", "v_vtxT2",
                            "v_vtxT3", "v_vtxPos" };
    char *body = (char *)mstring_get_str(output) + begin;
    for (char *p = body; *p; p++) {
        for (unsigned i = 0; i < ARRAY_SIZE(names); i++) {
            size_t n = strlen(names[i]);
            if (strncmp(p, names[i], n) || p[n] != '[') continue;
            char *close = p + n + 1;
            unsigned depth = 1;
            while (*close && depth) {
                depth += (*close == '[') - (*close == ']');
                if (depth) close++;
            }
            assert(!depth);
            p[0] = 'g';
            p[n] = '(';
            *close = ')';
            break;
        }
    }
}

static MString *pgraph_glsl_gen_geom_impl(const GeomState *state,
                                         GenGeomGlslOptions opts, bool dynamic)
{
    /* Both quad paths have identical native layouts. Their exact emitted
     * order and odd-strip suppression travel in a control bit instead. */
    GeomState canonical;
    if (dynamic && state->primitive_mode == PRIM_TYPE_QUAD_STRIP) {
        canonical = *state;
        canonical.primitive_mode = PRIM_TYPE_QUADS;
        state = &canonical;
    }
    /* FIXME: Missing support for 2-sided-poly mode */
    assert(state->polygon_front_mode == state->polygon_back_mode);
    enum ShaderPolygonMode polygon_mode = state->polygon_front_mode;

    bool need_triz = false;
    bool need_quadz = false;
    bool need_linez = false;
    bool need_wide = false;
    const char *layout_in = NULL;
    const char *layout_out = NULL;
    const char *body = NULL;
    const char *provoking_index = "0";

    /* TODO: frontface/backface culling for polygon modes POLY_MODE_LINE and
     * POLY_MODE_POINT.
     */
    switch (state->primitive_mode) {
    case PRIM_TYPE_POINTS: return NULL;
    case PRIM_TYPE_LINES:
    case PRIM_TYPE_LINE_LOOP:
    case PRIM_TYPE_LINE_STRIP:
        provoking_index = state->first_vertex_is_provoking ? "0" : "1";
        need_linez = true;
        layout_in = "layout(lines) in;\n";
        if (state->wide_lines) {
            need_wide = true;
            layout_out = "layout(triangle_strip, max_vertices = 4) out;\n";
            body = "  emit_wide_line(0, 1, 0.0);\n";
        } else {
            layout_out = "layout(line_strip, max_vertices = 2) out;\n";
            body = "  emit_line(0, 1, 0.0);\n";
        }
        break;
    case PRIM_TYPE_TRIANGLES:
    case PRIM_TYPE_TRIANGLE_STRIP:
    case PRIM_TYPE_TRIANGLE_FAN:
        if (state->first_vertex_is_provoking) {
            provoking_index = "v[0]";
        } else if (state->primitive_mode == PRIM_TYPE_TRIANGLE_STRIP) {
            provoking_index = "v[2 - (gl_PrimitiveIDIn & 1)]";
        } else if (state->primitive_mode == PRIM_TYPE_TRIANGLE_FAN) {
            provoking_index = "v[1]";
        } else {
            provoking_index = "v[2]";
        }
        need_triz = true;
        layout_in = "layout(triangles) in;\n";
        if (polygon_mode == POLY_MODE_FILL) {
            layout_out = "layout(triangle_strip, max_vertices = 3) out;\n";
            body = "  mat4 pz = calc_triz(v[0], v[1], v[2]);\n"
                   "  emit_vertex(v[0], pz);\n"
                   "  emit_vertex(v[1], pz);\n"
                   "  emit_vertex(v[2], pz);\n"
                   "  EndPrimitive();\n";
        } else if (polygon_mode == POLY_MODE_LINE) {
            need_linez = true;
            layout_out = "layout(line_strip, max_vertices = 6) out;\n";
            body = "  float dz = calc_triz(v[0], v[1], v[2])[3].x;\n"
                   "  emit_line(v[0], v[1], dz);\n"
                   "  emit_line(v[1], v[2], dz);\n"
                   "  emit_line(v[2], v[0], dz);\n";
        } else {
            assert(polygon_mode == POLY_MODE_POINT);
            layout_out = "layout(points, max_vertices = 3) out;\n";
            body = "  mat4 pz = calc_triz(v[0], v[1], v[2]);\n"
                   "  emit_vertex(v[0], mat4(pz[0], pz[0], pz[0], pz[3]));\n"
                   "  EndPrimitive();\n"
                   "  emit_vertex(v[1], mat4(pz[1], pz[1], pz[1], pz[3]));\n"
                   "  EndPrimitive();\n"
                   "  emit_vertex(v[2], mat4(pz[2], pz[2], pz[2], pz[3]));\n"
                   "  EndPrimitive();\n";
        }
        break;
    case PRIM_TYPE_QUADS:
        provoking_index = "3";
        need_quadz = true;
        layout_in = "layout(lines_adjacency) in;\n";
        if (polygon_mode == POLY_MODE_FILL) {
            layout_out = "layout(triangle_strip, max_vertices = 6) out;\n";
            body = "  mat4 pz, pz2;\n"
                   "  calc_quadz(0, 1, 2, 3, pz, pz2);\n"
                   "  emit_vertex(1, pz);\n"
                   "  emit_vertex(2, pz);\n"
                   "  emit_vertex(0, pz);\n"
                   "  EndPrimitive();\n"
                   "  emit_vertex(2, pz2);\n"
                   "  emit_vertex(3, pz2);\n"
                   "  emit_vertex(0, pz2);\n"
                   "  EndPrimitive();\n";
        } else if (polygon_mode == POLY_MODE_LINE) {
            need_linez = true;
            layout_out = "layout(line_strip, max_vertices = 8) out;\n";
            body = "  mat4 pz, pzs;\n"
                   "  calc_quadz(0, 1, 2, 3, pz, pzs);\n"
                   "  emit_line(0, 1, pz[3].x);\n"
                   "  emit_line(1, 2, pz[3].x);\n"
                   "  emit_line(2, 3, pzs[3].x);\n"
                   "  emit_line(3, 0, pzs[3].x);\n";
        } else {
            assert(polygon_mode == POLY_MODE_POINT);
            layout_out = "layout(points, max_vertices = 4) out;\n";
            body = "  mat4 pz, pz2;\n"
                   "  calc_quadz(0, 1, 2, 3, pz, pz2);\n"
                   "  emit_vertex(0, mat4(pz[0], pz[0], pz[0], pz[3]));\n"
                   "  EndPrimitive();\n"
                   "  emit_vertex(1, mat4(pz[1], pz[1], pz[1], pz[3]));\n"
                   "  EndPrimitive();\n"
                   "  emit_vertex(2, mat4(pz[2], pz[2], pz[2], pz[3]));\n"
                   "  EndPrimitive();\n"
                   "  emit_vertex(3, mat4(pz2[2], pz2[2], pz2[2], pz2[3]));\n"
                   "  EndPrimitive();\n";
        }
        if (dynamic) {
            if (polygon_mode == POLY_MODE_FILL) {
                body = "  bool strip=(gsControl.x&16)!=0;\n"
                       "  mat4 pz,pz2;\n"
                       "  calc_quadz(strip?2:0,strip?0:1,strip?1:2,3,pz,pz2);\n"
                       "  emit_vertex(strip?0:1,pz);\n"
                       "  emit_vertex(strip?1:2,pz);\n"
                       "  emit_vertex(strip?2:0,pz);EndPrimitive();\n"
                       "  emit_vertex(2,pz2);\n"
                       "  emit_vertex(strip?1:3,pz2);\n"
                       "  emit_vertex(strip?3:0,pz2);EndPrimitive();\n";
            } else if (polygon_mode == POLY_MODE_LINE) {
                body = "  bool strip=(gsControl.x&16)!=0;\n"
                       "  mat4 pz,pzs;\n"
                       "  calc_quadz(strip?2:0,strip?0:1,strip?1:2,3,pz,pzs);\n"
                       "  emit_line(0,1,pz[3].x);\n"
                       "  emit_line(1,strip?3:2,strip?pzs[3].x:pz[3].x);\n"
                       "  emit_line(strip?3:2,strip?2:3,pzs[3].x);\n"
                       "  emit_line(strip?2:3,0,strip?pz[3].x:pzs[3].x);\n";
            } else {
                body = "  bool strip=(gsControl.x&16)!=0;\n"
                       "  mat4 pz,pz2;\n"
                       "  calc_quadz(strip?2:0,strip?0:1,strip?1:2,3,pz,pz2);\n"
                       "  vec4 a=pz[strip?1:0],b=pz[strip?2:1],c=pz[strip?0:2];\n"
                       "  emit_vertex(0,mat4(a,a,a,pz[3]));EndPrimitive();\n"
                       "  emit_vertex(1,mat4(b,b,b,pz[3]));EndPrimitive();\n"
                       "  emit_vertex(2,mat4(c,c,c,pz[3]));EndPrimitive();\n"
                       "  emit_vertex(3,mat4(pz2[2],pz2[2],pz2[2],pz2[3]));EndPrimitive();\n";
            }
        }
        break;
    case PRIM_TYPE_QUAD_STRIP:
        provoking_index = "3";
        need_quadz = true;
        layout_in = "layout(lines_adjacency) in;\n";
        if (polygon_mode == POLY_MODE_FILL) {
            layout_out = "layout(triangle_strip, max_vertices = 6) out;\n";
            body = "  if ((gl_PrimitiveIDIn & 1) != 0) { return; }\n"
                   "  mat4 pz, pz2;\n"
                   "  calc_quadz(2, 0, 1, 3, pz, pz2);\n"
                   "  emit_vertex(0, pz);\n"
                   "  emit_vertex(1, pz);\n"
                   "  emit_vertex(2, pz);\n"
                   "  EndPrimitive();\n"
                   "  emit_vertex(2, pz2);\n"
                   "  emit_vertex(1, pz2);\n"
                   "  emit_vertex(3, pz2);\n"
                   "  EndPrimitive();\n";
        } else if (polygon_mode == POLY_MODE_LINE) {
            need_linez = true;
            layout_out = "layout(line_strip, max_vertices = 8) out;\n";
            body = "  if ((gl_PrimitiveIDIn & 1) != 0) { return; }\n"
                   "  mat4 pz, pzs;\n"
                   "  calc_quadz(2, 0, 1, 3, pz, pzs);\n"
                   "  emit_line(0, 1, pz[3].x);\n"
                   "  emit_line(1, 3, pzs[3].x);\n"
                   "  emit_line(3, 2, pzs[3].x);\n"
                   "  emit_line(2, 0, pz[3].x);\n";
        } else {
            assert(polygon_mode == POLY_MODE_POINT);
            layout_out = "layout(points, max_vertices = 4) out;\n";
            body = "  if ((gl_PrimitiveIDIn & 1) != 0) { return; }\n"
                   "  mat4 pz, pz2;\n"
                   "  calc_quadz(2, 0, 1, 3, pz, pz2);\n"
                   "  emit_vertex(0, mat4(pz[1], pz[1], pz[1], pz[3]));\n"
                   "  EndPrimitive();\n"
                   "  emit_vertex(1, mat4(pz[2], pz[2], pz[2], pz[3]));\n"
                   "  EndPrimitive();\n"
                   "  emit_vertex(2, mat4(pz[0], pz[0], pz[0], pz[3]));\n"
                   "  EndPrimitive();\n"
                   "  emit_vertex(3, mat4(pz2[2], pz2[2], pz2[2], pz2[3]));\n"
                   "  EndPrimitive();\n";
        }
        break;
    case PRIM_TYPE_POLYGON:
        if (polygon_mode == POLY_MODE_FILL) {
            provoking_index = "v[2]";
            need_triz = true;
            layout_in = "layout(triangles) in;\n";
            layout_out = "layout(triangle_strip, max_vertices = 3) out;\n";
            body = "  mat4 pz = calc_triz(v[0], v[1], v[2]);\n"
                   "  emit_vertex(v[0], pz);\n"
                   "  emit_vertex(v[1], pz);\n"
                   "  emit_vertex(v[2], pz);\n"
                   "  EndPrimitive();\n";
        } else if (polygon_mode == POLY_MODE_LINE) {
            provoking_index = "0";
            need_linez = true;
            /* FIXME: input here is lines and not triangles so we cannot
             * calculate triangle plane slope. Also, the first vertex of the
             * polygon is unavailable so flat shading provoking vertex is
             * wrong.
             */
            layout_in = "layout(lines) in;\n";
            layout_out = "layout(line_strip, max_vertices = 2) out;\n";
            body = "  emit_line(0, 1, 0.0);\n";
        } else {
            assert(!"Invalid polygon mode for PRIM_TYPE_POLYGON");
            return NULL;
        }
        break;

    default:
        assert(!"Invalid primitive mode");
        return NULL;
    }

    if (dynamic) {
        /* Atlas stage selection is carried in packed controls. Keep its
         * classifier available for every non-line source in the finite set. */
        opts.ui_clamp_stage = need_triz || need_quadz ? 1 : 0;
    }

    /* generate a geometry shader to support deprecated primitive types */
    assert(layout_in);
    assert(layout_out);
    assert(body);
    MString *output =
        mstring_from_fmt("#version %d\n\n"
                         "%s"
                         "%s"
                         "%s"
                         "\n"
                         "#define v_vtxPos v_vtxPos0\n"
                         "\n",
                         opts.vulkan ? 450 : (opts.locations ? 410 : 400),
                         opts.vulkan ? "" :
                             "in gl_PerVertex {\n"
                             "  vec4 gl_Position;\n"
                             "  float gl_PointSize;\n"
                             "} gl_in[];\n"
                             "out gl_PerVertex {\n"
                             "  vec4 gl_Position;\n"
                             "  float gl_PointSize;\n"
                             "};\n\n",
                         layout_in, layout_out);
    pgraph_glsl_get_vtx_header(output, opts.vulkan || opts.locations,
                               state->smooth_shading, true, true, true);
    pgraph_glsl_get_vtx_header(output, opts.vulkan || opts.locations,
                               state->smooth_shading, false, false, false);

    if (dynamic) {
        mstring_append(output, "uniform ivec4 gsControl;\n");
    }

    if (dynamic) {
        /* Keep every GS input load constant-indexed, including flat colors.
         * Some software GL implementations miscompile indirect GS inputs. */
        unsigned count = need_quadz ? 4 : need_triz ? 3 : 2;
        const char *names[] = { "vtxD0", "vtxD1", "vtxB0", "vtxB1",
                                "vtxFog", "vtxT0", "vtxT1", "vtxT2", "vtxT3",
                                "vtxPos", "Position", "PointSize" };
        for (unsigned i = 0; i < ARRAY_SIZE(names); i++) {
            mstring_append_fmt(output, "%s g_%s(int i){\n",
                               i == 4 || i == 11 ? "float" : "vec4", names[i]);
            for (unsigned j = 0; j < count; j++) {
                if (j + 1 < count) mstring_append_fmt(output,"if(i==%u)",j);
                if (i >= 10)
                    mstring_append_fmt(output,"return gl_in[%u].gl_%s;\n",j,names[i]);
                else
                    mstring_append_fmt(output,"return v_%s[%u];\n",
                                       i == 9 ? "vtxPos0" : names[i],j);
            }
            mstring_append(output, "}\n");
        }
    }
    if (dynamic && opts.ui_clamp_stage) {
        mstring_append(output,
            "vec4 gsUITex(int i){int t=(gsControl.x>>1)&7;\n"
            "if(t==1)return g_vtxT0(i);if(t==2)return g_vtxT1(i);\n"
            "if(t==3)return g_vtxT2(i);return g_vtxT3(i); }\n");
    }
    size_t dynamic_body_start = mstring_get_length(output);

    const char *ui_emit = "";
    const char *ui_setup = "";
    if (opts.ui_clamp_stage) {
        unsigned stage = opts.ui_clamp_stage - 1;
        unsigned count = state->primitive_mode == PRIM_TYPE_QUADS ||
                         state->primitive_mode == PRIM_TYPE_QUAD_STRIP ? 4 : 3;
        if (dynamic) append_ui_prepare_dynamic(output, count);
        else append_ui_prepare(output, stage, count);
        ui_emit = "if (uiValid) {\n    vtxPos1.xy = uiUVLo; vtxPos2.xy = uiUVHi;\n    triMZ = uintBitsToFloat(0x80000000u);\n}\n";
        ui_setup = state->primitive_mode == PRIM_TYPE_QUAD_STRIP ?
            "if ((gl_PrimitiveIDIn & 1) != 0) return; ui_prepare();\n" :
            "ui_prepare();\n";
        if (dynamic) {
            ui_setup = need_quadz ?
                "if ((gsControl.x&16)!=0 && (gl_PrimitiveIDIn&1)!=0) return;\n"
                "uiValid=false;if((gsControl.x&14)!=0)ui_prepare();\n" :
                "uiValid=false;if((gsControl.x&14)!=0)ui_prepare();\n";
        }
    }

    char vertex_order_buf[256];
    const char *vertex_order_body = "";

    if (need_triz) {
        /* Input triangle absolute vertex order is not guaranteed by OpenGL
         * or Vulkan, only winding order is. Reorder vertices here to first
         * vertex convention which we assumed above when setting
         * provoking_index. This mostly only matters with flat shading, but
         * we reorder always to get consistent results across GPU vendors
         * regarding floating-point rounding when calculating with vtxPos0/1/2.
         */
        mstring_append(output, "ivec3 v;\n");
        if (dynamic) {
            snprintf(vertex_order_buf, sizeof(vertex_order_buf),
                     "  int rot = (gl_PrimitiveIDIn & 1) == 0 ? gsControl.z : gsControl.w;\n"
                     "  v = rot == 0 ? ivec3(0, 1, 2) : rot == 1 ? ivec3(2, 0, 1) : ivec3(1, 2, 0);\n");
        } else if (state->tri_rot0 == state->tri_rot1) {
            snprintf(vertex_order_buf, sizeof(vertex_order_buf), "  v = %s;\n",
                     get_vertex_order(state->tri_rot0));
        } else {
            snprintf(vertex_order_buf, sizeof(vertex_order_buf),
                     "  v = (gl_PrimitiveIDIn & 1) == 0 ? %s : %s;\n",
                     get_vertex_order(state->tri_rot0),
                     get_vertex_order(state->tri_rot1));
        }
        vertex_order_body = vertex_order_buf;
    }

    if (dynamic) {
        provoking_index = need_triz ?
            "v[gsControl.y == 3 ? 2 - (gl_PrimitiveIDIn & 1) : gsControl.y]" :
            need_quadz ? "3" : "gsControl.y";
    }
    if (state->smooth_shading) {
        provoking_index = "index";
    }

    mstring_append_fmt(
        output,
        "void emit_vertex(int index, mat4 pz) {\n"
        "  gl_Position = %s;\n"
        "  gl_PointSize = %s;\n"
        "  vtxD0 = v_vtxD0[%s];\n"
        "  vtxD1 = v_vtxD1[%s];\n"
        "  vtxB0 = v_vtxB0[%s];\n"
        "  vtxB1 = v_vtxB1[%s];\n"
        "  vtxFog = v_vtxFog[index];\n"
        "  vtxT0 = v_vtxT0[index];\n"
        "  vtxT1 = v_vtxT1[index];\n"
        "  vtxT2 = v_vtxT2[index];\n"
        "  vtxT3 = v_vtxT3[index];\n"
        "  vtxPos0 = pz[0];\n"
        "  vtxPos1 = pz[1];\n"
        "  vtxPos2 = pz[2];\n"
        "  triMZ = (isnan(pz[3].x) || isinf(pz[3].x)) ? 0.0 : pz[3].x;\n"
        "%s"
        "  EmitVertex();\n"
        "}\n",
        dynamic ? "g_Position(index)" : "gl_in[index].gl_Position",
        dynamic ? "g_PointSize(index)" : "gl_in[index].gl_PointSize",
        provoking_index,
        provoking_index,
        provoking_index,
        provoking_index, ui_emit);

    if (need_triz || need_quadz) {
        mstring_append(
            output,
            // Kahan's algorithm for computing a*b - c*d using FMA for higher
            // precision. See e.g.:
            // Muller et al, "Handbook of Floating-Point Arithmetic", 2nd ed.
            // or
            // Claude-Pierre Jeannerod, Nicolas Louvet, and Jean-Michel Muller,
            // Further analysis of Kahan's algorithm for the accurate
            // computation of 2x2 determinants,
            // Mathematics of Computation 82(284), October 2013.
            "float kahan_det(float a, float b, float c, float d) {\n"
            "  precise float cd = c*d;\n"
            "  precise float err = fma(-c, d, cd);\n"
            "  precise float res = fma(a, b, -cd) + err;\n"
            "  return res;\n"
            "}\n");

        const char *ui_plane = opts.ui_clamp_stage ?
            "  if(uiValid) return mat4(v_vtxPos[i0],v_vtxPos[i1],v_vtxPos[i2],"
            "uintBitsToFloat(0x80000000u),vec3(0.0));\n" : "";
        if (dynamic) {
            mstring_append_fmt(
                output,
                "mat4 calc_triz(int i0, int i1, int i2) {\n"
                "%s"
                "  mat2 m = mat2(v_vtxPos[i1].xy - v_vtxPos[i0].xy,\n"
                "                v_vtxPos[i2].xy - v_vtxPos[i0].xy);\n"
                "  precise vec2 b;\n"
                "  if ((gsControl.x & 1) != 0) {\n"
                "    b = vec2(v_vtxPos[i0].w - v_vtxPos[i1].w,\n"
                "             v_vtxPos[i0].w - v_vtxPos[i2].w);\n"
                "    b /= vec2(v_vtxPos[i1].w, v_vtxPos[i2].w) * v_vtxPos[i0].w;\n"
                "  } else {\n"
                "    b = vec2(v_vtxPos[i1].z - v_vtxPos[i0].z,\n"
                "             v_vtxPos[i2].z - v_vtxPos[i0].z);\n"
                "  }\n"
                "  float det = kahan_det(m[0].x, m[1].y, m[1].x, m[0].y);\n"
                "  float dzx = kahan_det(b.x, m[1].y, b.y, m[0].y) / det;\n"
                "  float dzy = kahan_det(b.y, m[0].x, b.x, m[1].x) / det;\n"
                "  float dz = max(abs(dzx), abs(dzy));\n"
                "  return mat4(v_vtxPos[i0], v_vtxPos[i1], v_vtxPos[i2], dz, vec3(0.0));\n"
                "}\n", ui_plane);
        } else if (state->z_perspective) {
            mstring_append_fmt(
                output,
                "mat4 calc_triz(int i0, int i1, int i2) {\n"
                "%s"
                "  mat2 m = mat2(v_vtxPos[i1].xy - v_vtxPos[i0].xy,\n"
                "                v_vtxPos[i2].xy - v_vtxPos[i0].xy);\n"
                "  precise vec2 b = vec2(v_vtxPos[i0].w - v_vtxPos[i1].w,\n"
                "                        v_vtxPos[i0].w - v_vtxPos[i2].w);\n"
                "  b /= vec2(v_vtxPos[i1].w, v_vtxPos[i2].w) * v_vtxPos[i0].w;\n"
                // The following computes dzx and dzy same as
                // vec2 dz = b * inverse(m);
                "  float det = kahan_det(m[0].x, m[1].y, m[1].x, m[0].y);\n"
                "  float dzx = kahan_det(b.x, m[1].y, b.y, m[0].y) / det;\n"
                "  float dzy = kahan_det(b.y, m[0].x, b.x, m[1].x) / det;\n"
                "  float dz = max(abs(dzx), abs(dzy));\n"
                "  return mat4(v_vtxPos[i0], v_vtxPos[i1], v_vtxPos[i2], dz, vec3(0.0));\n"
                "}\n", ui_plane);
        } else {
            mstring_append_fmt(
                output,
                "mat4 calc_triz(int i0, int i1, int i2) {\n"
                "%s"
                "  mat2 m = mat2(v_vtxPos[i1].xy - v_vtxPos[i0].xy,\n"
                "                v_vtxPos[i2].xy - v_vtxPos[i0].xy);\n"
                "  precise vec2 b = vec2(v_vtxPos[i1].z - v_vtxPos[i0].z,\n"
                "                        v_vtxPos[i2].z - v_vtxPos[i0].z);\n"
                // The following computes dzx and dzy same as
                // vec2 dz = b * inverse(m);
                "  float det = kahan_det(m[0].x, m[1].y, m[1].x, m[0].y);\n"
                "  float dzx = kahan_det(b.x, m[1].y, b.y, m[0].y) / det;\n"
                "  float dzy = kahan_det(b.y, m[0].x, b.x, m[1].x) / det;\n"
                "  float dz = max(abs(dzx), abs(dzy));\n"
                "  return mat4(v_vtxPos[i0], v_vtxPos[i1], v_vtxPos[i2], dz, vec3(0.0));\n"
                "}\n", ui_plane);
        }
    }

    if (need_linez) {
        mstring_append(
            output,
            // Calculate a third vertex by rotating 90 degrees so that triangle
            // interpolation in fragment shader can be used as is for lines.
            "void emit_line(int i0, int i1, float dz) {\n"
            "  vec2 delta = v_vtxPos[i1].xy - v_vtxPos[i0].xy;\n"
            "  vec2 v2 = vec2(-delta.y, delta.x) + v_vtxPos[i0].xy;\n"
            "  mat4 pz = mat4(v_vtxPos[i0], v_vtxPos[i1], v2, v_vtxPos[i0].zw, dz, vec3(0.0));\n"
            "  emit_vertex(i0, pz);\n"
            "  emit_vertex(i1, pz);\n"
            "  EndPrimitive();\n"
            "}\n");
    }

    if (need_wide) {
        /* A segment as a rectangle of the guest's width: the width is in
         * guest pixels and the surface size in guest pixels too, so the
         * internal scale cancels (1 pixel = 2 / size in clip space). The
         * rectangle is emitted with the winding the culling state keeps,
         * since the hardware never culls a line. */
        mstring_append_fmt(
            output,
            "uniform vec2 gsSurfaceSize;\n"
            "uniform float gsLineWidth;\n"
            "uniform int gsKeepWinding;\n"
            "void emit_wide_vertex(int index, mat4 pz, vec4 pos) {\n"
            "  gl_Position = pos;\n"
            "  gl_PointSize = %s;\n"
            "  vtxD0 = v_vtxD0[%s];\n"
            "  vtxD1 = v_vtxD1[%s];\n"
            "  vtxB0 = v_vtxB0[%s];\n"
            "  vtxB1 = v_vtxB1[%s];\n"
            "  vtxFog = v_vtxFog[index];\n"
            "  vtxT0 = v_vtxT0[index];\n"
            "  vtxT1 = v_vtxT1[index];\n"
            "  vtxT2 = v_vtxT2[index];\n"
            "  vtxT3 = v_vtxT3[index];\n"
            "  vtxPos0 = pz[0];\n"
            "  vtxPos1 = pz[1];\n"
            "  vtxPos2 = pz[2];\n"
            "  triMZ = (isnan(pz[3].x) || isinf(pz[3].x)) ? 0.0 : pz[3].x;\n"
            "  EmitVertex();\n"
            "}\n"
            "void emit_wide_line(int i0, int i1, float dz) {\n"
            "  vec2 delta = v_vtxPos[i1].xy - v_vtxPos[i0].xy;\n"
            "  vec2 v2 = vec2(-delta.y, delta.x) + v_vtxPos[i0].xy;\n"
            "  mat4 pz = mat4(v_vtxPos[i0], v_vtxPos[i1], v2, v_vtxPos[i0].zw, dz, vec3(0.0));\n"
            "  vec4 p0 = %s;\n"
            "  vec4 p1 = %s;\n"
            "  vec2 s0 = p0.xy / p0.w;\n"
            "  vec2 s1 = p1.xy / p1.w;\n"
            "  vec2 d = (s1 - s0) * gsSurfaceSize;\n"
            "  float len = length(d);\n"
            "  vec2 n = (len > 0.0) ? vec2(-d.y, d.x) / len : vec2(0.0, 1.0);\n"
            "  vec2 off = n * gsLineWidth / gsSurfaceSize;\n"
            "  vec2 a = s0 + off;\n"
            "  vec2 b = s0 - off;\n"
            "  vec2 c = s1 + off;\n"
            "  float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);\n"
            "  if ((gsKeepWinding == 1 && area < 0.0) || (gsKeepWinding == 0 && area > 0.0)) {\n"
            "    off = -off;\n"
            "  }\n"
            "  emit_wide_vertex(i0, pz, vec4((s0 + off) * p0.w, p0.zw));\n"
            "  emit_wide_vertex(i0, pz, vec4((s0 - off) * p0.w, p0.zw));\n"
            "  emit_wide_vertex(i1, pz, vec4((s1 + off) * p1.w, p1.zw));\n"
            "  emit_wide_vertex(i1, pz, vec4((s1 - off) * p1.w, p1.zw));\n"
            "  EndPrimitive();\n"
            "}\n",
            dynamic ? "g_PointSize(index)" : "gl_in[index].gl_PointSize",
            provoking_index, provoking_index, provoking_index, provoking_index,
            dynamic ? "g_Position(i0)" : "gl_in[i0].gl_Position",
            dynamic ? "g_Position(i1)" : "gl_in[i1].gl_Position");
    }

    if (need_quadz) {
        mstring_append(
            output,
            "void calc_quadz(int i0, int i1, int i2, int i3, out mat4 triz1, out mat4 triz2) {\n"
            "  triz1 = calc_triz(i0, i1, i2);\n"
            "  triz2 = calc_triz(i0, i2, i3);\n"
            "}\n");
    }

    mstring_append_fmt(output,
                       "\n"
                       "void main() {\n"
                       "%s"
                       "%s"
                       "%s"
                       "}\n",
                       vertex_order_body, ui_setup, body);

    if (dynamic) geom_rewrite_inputs(output, dynamic_body_start);
    return output;
}

MString *pgraph_glsl_gen_geom(const GeomState *state, GenGeomGlslOptions opts)
{
    return pgraph_glsl_gen_geom_impl(state, opts, false);
}

MString *pgraph_glsl_gen_geom_dynamic(const GeomState *state,
                                    GenGeomGlslOptions opts)
{
    return pgraph_glsl_gen_geom_impl(state, opts, true);
}

/* Native topology/interpolation and the atlas stage are the remaining finite
 * source domain. Guest depth/provoking/winding controls travel separately. */
int pgraph_glsl_geom_dynamic_variant(const GeomState *state,
                                    GenGeomGlslOptions opts)
{
    if (state->polygon_front_mode != state->polygon_back_mode ||
        state->polygon_front_mode > POLY_MODE_LINE ||
        state->polygon_front_mode < POLY_MODE_FILL || opts.ui_clamp_stage > 4)
        return -1;
    unsigned family;
    switch (state->primitive_mode) {
    case PRIM_TYPE_LINES: case PRIM_TYPE_LINE_LOOP: case PRIM_TYPE_LINE_STRIP:
        family = state->wide_lines ? 1 : 0;
        break;
    case PRIM_TYPE_TRIANGLES: case PRIM_TYPE_TRIANGLE_STRIP:
    case PRIM_TYPE_TRIANGLE_FAN:
        family = 2 + state->polygon_front_mode;
        break;
    case PRIM_TYPE_QUADS: case PRIM_TYPE_QUAD_STRIP:
        family = 5 + state->polygon_front_mode;
        break;
    case PRIM_TYPE_POLYGON:
        if (state->polygon_front_mode == POLY_MODE_POINT) return -1;
        family = state->polygon_front_mode == POLY_MODE_FILL ? 2 : 0;
        break;
    default:
        return -1;
    }
    if (family < 2) {
        if (opts.ui_clamp_stage) return -1;
        return family * 2 + state->smooth_shading;
    }
    return 4 + (family - 2) * 2 + state->smooth_shading;
}

void pgraph_glsl_geom_dynamic_state(unsigned variant, GeomState *state,
                                    GenGeomGlslOptions *opts)
{
    assert(variant < GEOM_DYNAMIC_VARIANTS);
    memset(state, 0, sizeof(*state));
    *opts = (GenGeomGlslOptions){ .locations = true };
    state->smooth_shading = variant & 1;
    unsigned family;
    if (variant < 4) {
        family = variant / 2;
        state->primitive_mode = PRIM_TYPE_LINES;
        state->wide_lines = family == 1;
    } else {
        family = 2 + (variant - 4) / 2;
        state->primitive_mode = family < 5 ? PRIM_TYPE_TRIANGLES : PRIM_TYPE_QUADS;
        state->polygon_front_mode = (family - 2) % 3;
    }
    state->polygon_back_mode = state->polygon_front_mode;
}

void pgraph_glsl_geom_dynamic_controls(const GeomState *state, unsigned ui_stage,
                                      int controls[4])
{
    int provoking;
    switch (state->primitive_mode) {
    case PRIM_TYPE_LINES: case PRIM_TYPE_LINE_LOOP: case PRIM_TYPE_LINE_STRIP:
        provoking = state->first_vertex_is_provoking ? 0 : 1; break;
    case PRIM_TYPE_TRIANGLES: case PRIM_TYPE_TRIANGLE_STRIP:
    case PRIM_TYPE_TRIANGLE_FAN:
        provoking = state->first_vertex_is_provoking ? 0 :
                    state->primitive_mode == PRIM_TYPE_TRIANGLE_STRIP ? 3 :
                    state->primitive_mode == PRIM_TYPE_TRIANGLE_FAN ? 1 : 2;
        break;
    case PRIM_TYPE_POLYGON:
        provoking = state->polygon_front_mode == POLY_MODE_FILL ? 2 : 0; break;
    default:
        provoking = 3; break;
    }
    assert(ui_stage <= 4);
    controls[0] = state->z_perspective | (ui_stage << 1) |
                  (state->primitive_mode == PRIM_TYPE_QUAD_STRIP ? 16 : 0);
    controls[1] = provoking;
    controls[2] = state->tri_rot0 == 0 ? 0 : state->tri_rot0 == 1 ? 1 : 2;
    controls[3] = state->tri_rot1 == 0 ? 0 : state->tri_rot1 == 1 ? 1 : 2;
}
