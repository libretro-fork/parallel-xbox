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
 Bound,
};
#define HEADER(WG,DT) \
 SpvMagicNumber,0x00010000,0,Bound,0, \
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
 I(MemberDecorate,5),PushBlock,3,SpvDecorationOffset,12, \
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
 I(TypeStruct,6),PushBlock,UInt,UInt,UInt,UInt, \
 I(TypePointer,4),PushBlockPtr,SpvStorageClassPushConstant,PushBlock, \
 I(TypePointer,4),PushPtr,SpvStorageClassPushConstant,UInt, \
 I(Constant,4),UInt,C0,0, I(Constant,4),UInt,C1,1, I(Constant,4),UInt,C2,2, \
 I(Constant,4),UInt,C3,3, I(Constant,4),UInt,C4,4, I(Constant,4),UInt,C8,8, \
 I(Constant,4),UInt,C16,16, I(Constant,4),UInt,C24,24, I(Constant,4),UInt,Mask,255, \
 I(Constant,4),UInt,Workgroup,WG, \
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
#define PACK(WG,DT,CONVERT) \
 HEADER(WG,DT), I(UDiv,5),UInt,Scale,WidthIn,WidthOut, MAP(M0,Index,IMul), \
 I(AccessChain,6),DepthPtr,DPtr,Depth,C0,M0Index, CONVERT, \
 I(ShiftRightLogical,5),UInt,StencilIndex,M0Index,C2, \
 I(IAdd,5),UInt,StencilAddress,Base,StencilIndex, \
 I(AccessChain,6),UIntPtr,SDataPtr,Stencil,C0,StencilAddress, I(Load,4),UInt,SData,SDataPtr, \
 I(BitwiseAnd,5),UInt,Lane,M0Index,C3, I(ShiftLeftLogical,5),UInt,Shift,Lane,C3, \
 I(ShiftRightLogical,5),UInt,Byte,SData,Shift, I(BitwiseAnd,5),UInt,PValue,Byte,Mask, \
 I(ShiftLeftLogical,5),UInt,DepthShift,DValue,C8, I(BitwiseOr,5),UInt,PackedValue,DepthShift,PValue, \
 I(AccessChain,6),UIntPtr,PPtr,Packed,C0,Index, I(Store,3),PPtr,PackedValue, END
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
#define SPAN(x) (SpvStaticModule){x,sizeof(x)/sizeof(x[0])}
SpvStaticModule pgraph_spv_depth_compute(bool float_depth,bool pack,unsigned group_size)
{
    if(group_size==128){if(pack)return float_depth?SPAN(pack_f128):SPAN(pack_u128);return float_depth?SPAN(unpack_f128):SPAN(unpack_u128);}
    if(group_size==256){if(pack)return float_depth?SPAN(pack_f256):SPAN(pack_u256);return float_depth?SPAN(unpack_f256):SPAN(unpack_u256);}
    return (SpvStaticModule){0};
}
