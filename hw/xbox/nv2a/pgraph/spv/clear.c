/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <spirv/unified1/spirv.h>
#include "clear.h"

/* Instruction word counts include the opcode. Named IDs keep these fixed
 * programs reviewable without generated hex dumps or a build-time compiler. */
#define I(op, words) (((uint32_t)(words) << 16) | SpvOp##op)
enum {
    VVoid = 1, VFunction, VFloat, VVec4, VInt, VPerVertex,
    VInInt, VOutBlock, VOutVec4, VIndex, VOutput,
    VZero, VOne, VTwo, VFZero, VFOne, VFMinusOne,
    VMain, VEntry, VLoadedIndex, VXBits, VXShift, VXFloat, VX,
    VYBits, VYShift, VYFloat, VY, VPosition, VPositionPtr, VBound,
};
static const uint32_t fullscreen_vertex[] = {
    SpvMagicNumber, 0x00010000, 0, VBound, 0,
    I(Capability, 2), SpvCapabilityShader,
    I(MemoryModel, 3), SpvAddressingModelLogical, SpvMemoryModelGLSL450,
    I(EntryPoint, 7), SpvExecutionModelVertex, VMain, 0x6e69616d, 0, VIndex, VOutput,
    I(Decorate, 4), VIndex, SpvDecorationBuiltIn, SpvBuiltInVertexIndex,
    I(Decorate, 3), VPerVertex, SpvDecorationBlock,
    I(MemberDecorate, 5), VPerVertex, 0, SpvDecorationBuiltIn, SpvBuiltInPosition,
    I(TypeVoid, 2), VVoid,
    I(TypeFunction, 3), VFunction, VVoid,
    I(TypeFloat, 3), VFloat, 32,
    I(TypeVector, 4), VVec4, VFloat, 4,
    I(TypeInt, 4), VInt, 32, 1,
    I(TypeStruct, 3), VPerVertex, VVec4,
    I(TypePointer, 4), VInInt, SpvStorageClassInput, VInt,
    I(TypePointer, 4), VOutBlock, SpvStorageClassOutput, VPerVertex,
    I(TypePointer, 4), VOutVec4, SpvStorageClassOutput, VVec4,
    I(Constant, 4), VInt, VZero, 0,
    I(Constant, 4), VInt, VOne, 1,
    I(Constant, 4), VInt, VTwo, 2,
    I(Constant, 4), VFloat, VFZero, 0,
    I(Constant, 4), VFloat, VFOne, 0x3f800000,
    I(Constant, 4), VFloat, VFMinusOne, 0xbf800000,
    I(Variable, 4), VInInt, VIndex, SpvStorageClassInput,
    I(Variable, 4), VOutBlock, VOutput, SpvStorageClassOutput,
    I(Function, 5), VVoid, VMain, SpvFunctionControlMaskNone, VFunction,
    I(Label, 2), VEntry,
    I(Load, 4), VInt, VLoadedIndex, VIndex,
    /* x = -1 + float((VertexIndex & 1) << 2), y uses bits 2 and shift 1. */
    I(BitwiseAnd, 5), VInt, VXBits, VLoadedIndex, VOne,
    I(ShiftLeftLogical, 5), VInt, VXShift, VXBits, VTwo,
    I(ConvertSToF, 4), VFloat, VXFloat, VXShift,
    I(FAdd, 5), VFloat, VX, VFMinusOne, VXFloat,
    I(BitwiseAnd, 5), VInt, VYBits, VLoadedIndex, VTwo,
    I(ShiftLeftLogical, 5), VInt, VYShift, VYBits, VOne,
    I(ConvertSToF, 4), VFloat, VYFloat, VYShift,
    I(FAdd, 5), VFloat, VY, VFMinusOne, VYFloat,
    I(CompositeConstruct, 7), VVec4, VPosition, VX, VY, VFZero, VFOne,
    I(AccessChain, 5), VOutVec4, VPositionPtr, VOutput, VZero,
    I(Store, 3), VPositionPtr, VPosition,
    I(Return, 1), I(FunctionEnd, 1),
};

enum { FVoid = 1, FFunction, FFloat, FVec4, FOutVec4, FOne, FWhite,
       FOutput, FMain, FEntry, FBound };
static const uint32_t solid_fragment[] = {
    SpvMagicNumber, 0x00010000, 0, FBound, 0,
    I(Capability, 2), SpvCapabilityShader,
    I(MemoryModel, 3), SpvAddressingModelLogical, SpvMemoryModelGLSL450,
    I(EntryPoint, 6), SpvExecutionModelFragment, FMain, 0x6e69616d, 0, FOutput,
    I(ExecutionMode, 3), FMain, SpvExecutionModeOriginUpperLeft,
    I(Decorate, 4), FOutput, SpvDecorationLocation, 0,
    I(TypeVoid, 2), FVoid,
    I(TypeFunction, 3), FFunction, FVoid,
    I(TypeFloat, 3), FFloat, 32,
    I(TypeVector, 4), FVec4, FFloat, 4,
    I(TypePointer, 4), FOutVec4, SpvStorageClassOutput, FVec4,
    I(Constant, 4), FFloat, FOne, 0x3f800000,
    I(ConstantComposite, 7), FVec4, FWhite, FOne, FOne, FOne, FOne,
    I(Variable, 4), FOutVec4, FOutput, SpvStorageClassOutput,
    I(Function, 5), FVoid, FMain, SpvFunctionControlMaskNone, FFunction,
    I(Label, 2), FEntry,
    I(Store, 3), FOutput, FWhite,
    I(Return, 1), I(FunctionEnd, 1),
};
#undef I

SpvStaticModule pgraph_spv_fullscreen_vertex(void)
{
    return (SpvStaticModule) { fullscreen_vertex,
        sizeof(fullscreen_vertex) / sizeof(fullscreen_vertex[0]) };
}
SpvStaticModule pgraph_spv_solid_fragment(void)
{
    return (SpvStaticModule) { solid_fragment,
        sizeof(solid_fragment) / sizeof(solid_fragment[0]) };
}
