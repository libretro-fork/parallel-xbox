/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <spirv/unified1/spirv.h>
#include "depth.h"
#define I(op,n) (((uint32_t)(n)<<16)|SpvOp##op)
#define MAP_IDS(p) p##Y,p##X,p##SY,p##SX,p##Row,p##Index
/* Names describe the SSA values; no source compiler or mutable word template
 * is involved. The only repeated arrays differ by workgroup size/type. */
enum {
 Void=1,Fn,Bool,UInt,SInt,Float,UVec3,InVec3Ptr,
 DepthArray,DepthBlock,DepthBlockPtr,DepthPtr,UIntArray,UIntBlock,UIntBlockPtr,UIntPtr,
 PushBlock,PushBlockPtr,PushPtr,Global,Groups,PC,Depth,Stencil,Packed,
 C0,C1,C2,C3,C4,C8,C16,C24,Mask,Workgroup,FDepthMax,FDepthDiv,
 Main,Entry,Body,Done,Gid,Ngroups,Gx,Gy,Nx,Stride,RowOffset,Index,CountPtr,Count,Valid,
 WidthInPtr,WidthOutPtr,BasePtr,WidthIn,WidthOut,Base,Scale,
 MAP_IDS(M0),MAP_IDS(M1),MAP_IDS(M2),MAP_IDS(M3),
 DPtr,DValue,FValue,FProduct,SValue,PPtr,PValue,
 StencilIndex,StencilAddress,SDataPtr,SData,Lane,Shift,Byte,DepthShift,PackedValue,
 FConverted,FDivided,FBits,FBitsAdjusted,FOut,
 AlignedBits,Aligned,StencilBody,StencilDone,
 Index1,Index2,Index3,PPtr1,PPtr2,PPtr3,PValue1,PValue2,PValue3,
 B0,B1,B2,B3,B1Shift,B2Shift,B3Shift,B01,B012,BAll,
 OldBound,
 FHeight,FRect,FRectMask,FNotRectMask,FSmallX,FSmallY,FHigh,
 FX8,FX8Or,FX8Mask,FX4,FX4Or,FX4Mask,FX2,FX2Or,FX2Mask,FX1,FX1Or,FX1Mask,
 FY8,FY8Or,FY8Mask,FY4,FY4Or,FY4Mask,FY2,FY2Or,FY2Mask,FY1,FY1Or,FY1Mask,
 FYShift,FDstPtr,FDst,FOutput,
 FHighCoord,FLowX,FLowY,FHighX,FHighY,FFFFMask,
 FWide,FMask8,FMask4,FMask2,FMask1,
 Bound,
};
#define HEADER_LAYOUT(WG,DT,EXTRA_PUSH,PUSH_TYPE,EXTRA_CONSTANTS,MODULE_BOUND) \
 SpvMagicNumber,0x00010000,0,MODULE_BOUND,0, \
 I(Capability,2),SpvCapabilityShader, \
 I(MemoryModel,3),SpvAddressingModelLogical,SpvMemoryModelGLSL450, \
 I(EntryPoint,7),SpvExecutionModelGLCompute,Main,0x6e69616d,0,Global,Groups, \
 I(ExecutionMode,6),Main,SpvExecutionModeLocalSize,WG,1,1, \
 I(Decorate,4),Global,SpvDecorationBuiltIn,SpvBuiltInGlobalInvocationId, \
 I(Decorate,4),Groups,SpvDecorationBuiltIn,SpvBuiltInNumWorkgroups, \
 I(Decorate,4),DepthArray,SpvDecorationArrayStride,4, \
 I(Decorate,4),UIntArray,SpvDecorationArrayStride,4, \
 I(Decorate,3),DepthBlock,SpvDecorationBufferBlock, \
 I(Decorate,3),UIntBlock,SpvDecorationBufferBlock, \
 I(MemberDecorate,5),DepthBlock,0,SpvDecorationOffset,0, \
 I(MemberDecorate,5),UIntBlock,0,SpvDecorationOffset,0, \
 I(Decorate,3),PushBlock,SpvDecorationBlock, \
 I(MemberDecorate,5),PushBlock,0,SpvDecorationOffset,0, \
 I(MemberDecorate,5),PushBlock,1,SpvDecorationOffset,4, \
 I(MemberDecorate,5),PushBlock,2,SpvDecorationOffset,8, \
 I(MemberDecorate,5),PushBlock,3,SpvDecorationOffset,12, EXTRA_PUSH \
 I(Decorate,4),Depth,SpvDecorationDescriptorSet,0, \
 I(Decorate,4),Stencil,SpvDecorationDescriptorSet,0, \
 I(Decorate,4),Packed,SpvDecorationDescriptorSet,0, \
 I(Decorate,4),Depth,SpvDecorationBinding,0, \
 I(Decorate,4),Stencil,SpvDecorationBinding,1, \
 I(Decorate,4),Packed,SpvDecorationBinding,2, \
 I(TypeVoid,2),Void, I(TypeFunction,3),Fn,Void, I(TypeBool,2),Bool, \
 I(TypeInt,4),UInt,32,0, I(TypeInt,4),SInt,32,1, I(TypeFloat,3),Float,32, \
 I(TypeVector,4),UVec3,UInt,3, I(TypePointer,4),InVec3Ptr,SpvStorageClassInput,UVec3, \
 I(TypeRuntimeArray,3),DepthArray,DT, I(TypeStruct,3),DepthBlock,DepthArray, \
 I(TypePointer,4),DepthBlockPtr,SpvStorageClassUniform,DepthBlock, \
 I(TypePointer,4),DepthPtr,SpvStorageClassUniform,DT, \
 I(TypeRuntimeArray,3),UIntArray,UInt, I(TypeStruct,3),UIntBlock,UIntArray, \
 I(TypePointer,4),UIntBlockPtr,SpvStorageClassUniform,UIntBlock, \
 I(TypePointer,4),UIntPtr,SpvStorageClassUniform,UInt, \
 PUSH_TYPE \
 I(TypePointer,4),PushBlockPtr,SpvStorageClassPushConstant,PushBlock, \
 I(TypePointer,4),PushPtr,SpvStorageClassPushConstant,UInt, \
 I(Constant,4),UInt,C0,0, I(Constant,4),UInt,C1,1, I(Constant,4),UInt,C2,2, \
 I(Constant,4),UInt,C3,3, I(Constant,4),UInt,C4,4, I(Constant,4),UInt,C8,8, \
 I(Constant,4),UInt,C16,16, I(Constant,4),UInt,C24,24, I(Constant,4),UInt,Mask,255, \
 I(Constant,4),UInt,Workgroup,WG, EXTRA_CONSTANTS \
 I(Constant,4),Float,FDepthMax,0x4b7fffff, I(Constant,4),Float,FDepthDiv,0x4b800000, \
 I(Variable,4),InVec3Ptr,Global,SpvStorageClassInput, \
 I(Variable,4),InVec3Ptr,Groups,SpvStorageClassInput, \
 I(Variable,4),PushBlockPtr,PC,SpvStorageClassPushConstant, \
 I(Variable,4),DepthBlockPtr,Depth,SpvStorageClassUniform, \
 I(Variable,4),UIntBlockPtr,Stencil,SpvStorageClassUniform, \
 I(Variable,4),UIntBlockPtr,Packed,SpvStorageClassUniform, \
 I(Function,5),Void,Main,SpvFunctionControlMaskNone,Fn, I(Label,2),Entry, \
 I(Load,4),UVec3,Gid,Global, I(Load,4),UVec3,Ngroups,Groups, \
 I(CompositeExtract,5),UInt,Gx,Gid,0, I(CompositeExtract,5),UInt,Gy,Gid,1, \
 I(CompositeExtract,5),UInt,Nx,Ngroups,0, \
 I(IMul,5),UInt,Stride,Nx,Workgroup, I(IMul,5),UInt,RowOffset,Gy,Stride, \
 I(IAdd,5),UInt,Index,Gx,RowOffset, \
 I(AccessChain,5),PushPtr,CountPtr,PC,C3, I(Load,4),UInt,Count,CountPtr, \
 I(ULessThan,5),Bool,Valid,Index,Count, I(SelectionMerge,3),Done,SpvSelectionControlMaskNone, \
 I(BranchConditional,4),Valid,Body,Done, I(Label,2),Body, \
 I(AccessChain,5),PushPtr,WidthInPtr,PC,C0, I(Load,4),UInt,WidthIn,WidthInPtr, \
 I(AccessChain,5),PushPtr,WidthOutPtr,PC,C1, I(Load,4),UInt,WidthOut,WidthOutPtr, \
 I(AccessChain,5),PushPtr,BasePtr,PC,C2, I(Load,4),UInt,Base,BasePtr
#define PUSH4 I(TypeStruct,6),PushBlock,UInt,UInt,UInt,UInt,
#define HEADER(WG,DT) HEADER_LAYOUT(WG,DT,,PUSH4,,OldBound)
#define MAP(p,idx,op) \
 I(UDiv,5),UInt,p##Y,idx,WidthOut, I(UMod,5),UInt,p##X,idx,WidthOut, \
 I(op,5),UInt,p##SY,p##Y,Scale, I(op,5),UInt,p##SX,p##X,Scale, \
 I(IMul,5),UInt,p##Row,p##SY,WidthIn, I(IAdd,5),UInt,p##Index,p##Row,p##SX
#define END I(Branch,2),Done,I(Label,2),Done,I(Return,1),I(FunctionEnd,1)
#define PACK_UINT I(Load,4),UInt,DValue,DPtr
/* Preserve the original signed float-to-int conversion, then uint bits. */
#define PACK_FLOAT \
 I(Load,4),Float,FValue,DPtr, I(FMul,5),Float,FProduct,FValue,FDepthMax, \
 I(ConvertFToS,4),SInt,SValue,FProduct, I(Bitcast,4),UInt,DValue,SValue
#define PACK_VALUE(...) \
 I(AccessChain,6),DepthPtr,DPtr,Depth,C0,M0Index, __VA_ARGS__, \
 I(ShiftRightLogical,5),UInt,StencilIndex,M0Index,C2, \
 I(IAdd,5),UInt,StencilAddress,Base,StencilIndex, \
 I(AccessChain,6),UIntPtr,SDataPtr,Stencil,C0,StencilAddress, I(Load,4),UInt,SData,SDataPtr, \
 I(BitwiseAnd,5),UInt,Lane,M0Index,C3, I(ShiftLeftLogical,5),UInt,Shift,Lane,C3, \
 I(ShiftRightLogical,5),UInt,Byte,SData,Shift, I(BitwiseAnd,5),UInt,PValue,Byte,Mask, \
 I(ShiftLeftLogical,5),UInt,DepthShift,DValue,C8, I(BitwiseOr,5),UInt,PackedValue,DepthShift,PValue,
#define PACK(WG,DT,CONVERT) \
 HEADER(WG,DT), I(UDiv,5),UInt,Scale,WidthIn,WidthOut, MAP(M0,Index,IMul), \
 PACK_VALUE(CONVERT) \
 I(AccessChain,6),UIntPtr,PPtr,Packed,C0,Index, I(Store,3),PPtr,PackedValue, END

/* Read linear device-memory planes in Morton order and write contiguous guest
 * words. Keep host writes contiguous: scattered stores are costly across PCIe.
 * Binding 2 is the final guest buffer; the fifth push word is its word offset. */
#define PUSH5 I(TypeStruct,7),PushBlock,UInt,UInt,UInt,UInt,UInt,
#define PUSH5_OFFSET I(MemberDecorate,5),PushBlock,4,SpvDecorationOffset,16,
#define COMPACT_CONSTANTS \
 I(Constant,4),UInt,FMask8,0x00ff00ff, \
 I(Constant,4),UInt,FMask4,0x0f0f0f0f, \
 I(Constant,4),UInt,FMask2,0x33333333, \
 I(Constant,4),UInt,FMask1,0x55555555, \
 I(Constant,4),UInt,FFFFMask,0x0000ffff,
#define COMPACT_STEP(P,INPUT,N,C,MASK) \
 I(ShiftRightLogical,5),UInt,P##N,INPUT,C, \
 I(BitwiseOr,5),UInt,P##N##Or,INPUT,P##N, \
 I(BitwiseAnd,5),UInt,P##N##Mask,P##N##Or,MASK
#define COMPACT(P,INPUT) \
 COMPACT_STEP(P,INPUT,1,C1,FMask2), \
 COMPACT_STEP(P,P##1Mask,2,C2,FMask4), \
 COMPACT_STEP(P,P##2Mask,4,C4,FMask8), \
 COMPACT_STEP(P,P##4Mask,8,C8,FFFFMask)
#define PACK_SWIZZLED(WG,DT,CONVERT) \
 HEADER_LAYOUT(WG,DT,PUSH5_OFFSET,PUSH5,COMPACT_CONSTANTS,Bound), \
 I(UDiv,5),UInt,Scale,WidthIn,WidthOut, \
 I(UDiv,5),UInt,FHeight,Count,WidthOut, \
 I(ULessThan,5),Bool,FWide,WidthOut,FHeight, \
 I(Select,6),UInt,FRect,FWide,WidthOut,FHeight, \
 I(ISub,5),UInt,FRectMask,FRect,C1, I(Not,4),UInt,FNotRectMask,FRectMask, \
 I(BitwiseAnd,5),UInt,FSmallX,Index,FMask1, \
 I(ShiftRightLogical,5),UInt,FYShift,Index,C1, \
 I(BitwiseAnd,5),UInt,FSmallY,FYShift,FMask1, \
 COMPACT(FX,FSmallX), COMPACT(FY,FSmallY), \
 I(BitwiseAnd,5),UInt,FLowX,FX8Mask,FRectMask, \
 I(BitwiseAnd,5),UInt,FLowY,FY8Mask,FRectMask, \
 I(UDiv,5),UInt,FHighCoord,Index,FRect, \
 I(BitwiseAnd,5),UInt,FHigh,FHighCoord,FNotRectMask, \
 I(Select,6),UInt,FHighX,FWide,C0,FHigh, \
 I(Select,6),UInt,FHighY,FWide,FHigh,C0, \
 I(BitwiseOr,5),UInt,M0X,FLowX,FHighX, \
 I(BitwiseOr,5),UInt,M0Y,FLowY,FHighY, \
 I(IMul,5),UInt,M0SY,M0Y,Scale, I(IMul,5),UInt,M0SX,M0X,Scale, \
 I(IMul,5),UInt,M0Row,M0SY,WidthIn, I(IAdd,5),UInt,M0Index,M0Row,M0SX, \
 PACK_VALUE(CONVERT) \
 I(AccessChain,5),PushPtr,FDstPtr,PC,C4, I(Load,4),UInt,FDst,FDstPtr, \
 I(IAdd,5),UInt,FOutput,FDst,Index, \
 I(AccessChain,6),UIntPtr,PPtr,Packed,C0,FOutput, I(Store,3),PPtr,PackedValue, END
#define UNPACK_UINT
#define UNPACK_FLOAT \
 I(ConvertUToF,4),Float,FConverted,DValue, I(FDiv,5),Float,FDivided,FConverted,FDepthDiv, \
 I(Bitcast,4),UInt,FBits,FDivided, I(IAdd,5),UInt,FBitsAdjusted,FBits,C1, \
 I(Bitcast,4),Float,FOut,FBitsAdjusted,
#define STENCIL_BYTE(N,C,SHIFT) \
 I(IAdd,5),UInt,Index##N,Index,C, MAP(M##N,Index##N,UDiv), \
 I(AccessChain,6),UIntPtr,PPtr##N,Packed,C0,M##N##Index, \
 I(Load,4),UInt,PValue##N,PPtr##N, I(BitwiseAnd,5),UInt,B##N,PValue##N,Mask, \
 I(ShiftLeftLogical,5),UInt,B##N##Shift,B##N,SHIFT
#define UNPACK(WG,DT,CONVERT,RESULT) \
 HEADER(WG,DT), I(UDiv,5),UInt,Scale,WidthOut,WidthIn, MAP(M0,Index,UDiv), \
 I(AccessChain,6),UIntPtr,PPtr,Packed,C0,M0Index, I(Load,4),UInt,PValue,PPtr, \
 I(ShiftRightLogical,5),UInt,DValue,PValue,C8, CONVERT \
 I(AccessChain,6),DepthPtr,DPtr,Depth,C0,Index, I(Store,3),DPtr,RESULT, \
 I(BitwiseAnd,5),UInt,AlignedBits,Index,C3, I(IEqual,5),Bool,Aligned,AlignedBits,C0, \
 I(SelectionMerge,3),StencilDone,SpvSelectionControlMaskNone, \
 I(BranchConditional,4),Aligned,StencilBody,StencilDone, I(Label,2),StencilBody, \
 I(BitwiseAnd,5),UInt,B0,PValue,Mask, \
 STENCIL_BYTE(1,C1,C8),STENCIL_BYTE(2,C2,C16),STENCIL_BYTE(3,C3,C24), \
 I(BitwiseOr,5),UInt,B01,B0,B1Shift, I(BitwiseOr,5),UInt,B012,B01,B2Shift, \
 I(BitwiseOr,5),UInt,BAll,B012,B3Shift, \
 I(ShiftRightLogical,5),UInt,StencilIndex,Index,C2, I(IAdd,5),UInt,StencilAddress,Base,StencilIndex, \
 I(AccessChain,6),UIntPtr,SDataPtr,Stencil,C0,StencilAddress, I(Store,3),SDataPtr,BAll, \
 I(Branch,2),StencilDone,I(Label,2),StencilDone,END
#define KERNELS(WG) \
 static const uint32_t pack_u##WG[]={PACK(WG,UInt,PACK_UINT)}; \
 static const uint32_t pack_f##WG[]={PACK(WG,Float,PACK_FLOAT)}; \
 static const uint32_t unpack_u##WG[]={UNPACK(WG,UInt,UNPACK_UINT,DValue)}; \
 static const uint32_t unpack_f##WG[]={UNPACK(WG,Float,UNPACK_FLOAT,FOut)}
KERNELS(128);
KERNELS(256);
#define SWIZZLED_KERNELS(WG) \
 static const uint32_t swizzled_u##WG[]={PACK_SWIZZLED(WG,UInt,PACK_UINT)}; \
 static const uint32_t swizzled_f##WG[]={PACK_SWIZZLED(WG,Float,PACK_FLOAT)}
SWIZZLED_KERNELS(128);
SWIZZLED_KERNELS(256);
#define SPAN(x) (SpvStaticModule){x,sizeof(x)/sizeof(x[0])}
SpvStaticModule pgraph_spv_depth_compute(bool float_depth,bool pack,unsigned group_size)
{
    if(group_size==128){if(pack)return float_depth?SPAN(pack_f128):SPAN(pack_u128);return float_depth?SPAN(unpack_f128):SPAN(unpack_u128);}
    if(group_size==256){if(pack)return float_depth?SPAN(pack_f256):SPAN(pack_u256);return float_depth?SPAN(unpack_f256):SPAN(unpack_u256);}
    return (SpvStaticModule){0};
}

SpvStaticModule pgraph_spv_depth_swizzled(bool float_depth,unsigned group_size)
{
    if(group_size==128)return float_depth?SPAN(swizzled_f128):SPAN(swizzled_u128);
    if(group_size==256)return float_depth?SPAN(swizzled_f256):SPAN(swizzled_u256);
    return (SpvStaticModule){0};
}
