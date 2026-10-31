/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (c) 2026 Reda Cherif-Touil */
#include <spirv/unified1/spirv.h>
#include "swizzle.h"

/* Fixed Morton-order conversion programs. The push block and buffer bindings
 * match vk/unswizzle.c. Named SSA IDs describe the original integer operations;
 * neither a source compiler nor mutable module templates run in the core. */
#define I(op,n) (((uint32_t)(n)<<16)|SpvOp##op)
enum {
 Main = 1, Global, Words, Buffer, Vram, Linear, PushBlock, Void,
 Bool, UInt, UVec3, MainType, MapType, BufferPtr, WordPtr, PushBlockPtr,
 PushPtr, InputPtr, C0, C1, C2, C3, C4, C5,
 C6, C7, C8, C9, C10, C16, C255, C65535,
 PC, Entry, Gid, Index, CountPtr, Count, InBounds, Done,
 Body, SrcPtr, Src, DstPtr, Dst, WidthPtr, Width, HeightPtr,
 Height, DepthPtr, Depth, BppPtr, Bpp, MaskXPtr, MaskX, MaskYPtr,
 MaskY, MaskZPtr, MaskZ, PalettePtr, Palette, Paletted, OutputBpp, PerWord,
 First, PlaneSize, Texels, Loop, K, NextK, Continue, R,
 NextR, More, LoopDone, LoopBody, Texel, ValidTexel, TailDone, ReadTexel,
 X, Row, Y, Z, SX, Map, SY, SZ,
 SXY, Swizzled, TexelBytes, Offset, WordIndex, SourcePtr, Word, IsWord,
 ValueDone, WholeWord, SmallValue, IsHalf, SmallDone, HalfValue, ByteValue, HalfLane,
 HalfShift, HalfBits, Half, ByteLane, ByteShift, ByteBits, Byte, PaletteDone,
 ReadPalette, PaletteBase, PaletteIndex, Color, ByteOrColor, Small, Value, LaneBytes,
 Shift, ShiftedValue, Combined, DstWord, OutputIndex, OutputPtr, LinearTexel, MapValue,
 MapMask, MapEntry, MapLoop, Mask, NextMask, MapContinue, Bit, NextBit,
 Result, NextResult, HasMask, MapDone, MapBody, NegativeMask, Low, TestBit,
 Set, Selected, NotLow, PaletteDataPtr, LinearBuffer, LinearBufferPtr, FetchedValue, FetchTexel,
 FetchType, TTexel, FetchEntry, TPlaneSize, TPaletted, TSrcPtr, TSrc, TWidthPtr,
 TWidth, THeightPtr, THeight, TBppPtr, TBpp, TMaskXPtr, TMaskX, TMaskYPtr,
 TMaskY, TMaskZPtr, TMaskZ, TPalettePtr, TPalette, Bound,
};
#define HEADER(READ,WRITE) \
I(Capability,2), SpvCapabilityShader, \
 I(MemoryModel,3), SpvAddressingModelLogical, SpvMemoryModelGLSL450, \
 I(EntryPoint,6), SpvExecutionModelGLCompute, Main, 0x6e69616d, 0, Global, \
 I(ExecutionMode,6), Main, SpvExecutionModeLocalSize, PGRAPH_SPV_SWIZZLE_WORKGROUP, 1, 1, \
 I(Decorate,4), Words, SpvDecorationArrayStride, 4, \
 I(Decorate,3), Buffer, SpvDecorationBufferBlock, \
 I(Decorate,3), LinearBuffer, SpvDecorationBufferBlock, \
 I(MemberDecorate,5), LinearBuffer, 0, SpvDecorationOffset, 0, \
 I(MemberDecorate,5), Buffer, 0, SpvDecorationOffset, 0, \
 I(Decorate,4), Vram, SpvDecorationDescriptorSet, 0, \
 I(Decorate,4), Vram, SpvDecorationBinding, 0, \
 I(Decorate,4), Linear, SpvDecorationDescriptorSet, 0, \
 I(Decorate,4), Linear, SpvDecorationBinding, 1, \
 I(Decorate,4), Global, SpvDecorationBuiltIn, SpvBuiltInGlobalInvocationId, \
 I(MemberDecorate,4), READ, 0, SpvDecorationNonWritable, \
 I(MemberDecorate,4), WRITE, 0, SpvDecorationNonReadable, \
 I(Decorate,3), PushBlock, SpvDecorationBlock, \
 I(MemberDecorate,5), PushBlock, 0, SpvDecorationOffset, 0, \
 I(MemberDecorate,5), PushBlock, 1, SpvDecorationOffset, 4, \
 I(MemberDecorate,5), PushBlock, 2, SpvDecorationOffset, 8, \
 I(MemberDecorate,5), PushBlock, 3, SpvDecorationOffset, 12, \
 I(MemberDecorate,5), PushBlock, 4, SpvDecorationOffset, 16, \
 I(MemberDecorate,5), PushBlock, 5, SpvDecorationOffset, 20, \
 I(MemberDecorate,5), PushBlock, 6, SpvDecorationOffset, 24, \
 I(MemberDecorate,5), PushBlock, 7, SpvDecorationOffset, 28, \
 I(MemberDecorate,5), PushBlock, 8, SpvDecorationOffset, 32, \
 I(MemberDecorate,5), PushBlock, 9, SpvDecorationOffset, 36, \
 I(MemberDecorate,5), PushBlock, 10, SpvDecorationOffset, 40, \
 \
 I(TypeVoid,2), Void, \
 I(TypeBool,2), Bool, \
 I(TypeInt,4), UInt, 32, 0, \
 I(TypeVector,4), UVec3, UInt, 3, \
 I(TypeFunction,3), MainType, Void, \
 I(TypeFunction,5), MapType, UInt, UInt, UInt, \
 I(TypeFunction,4), FetchType, UInt, UInt, \
 I(TypeRuntimeArray,3), Words, UInt, \
 I(TypeStruct,3), Buffer, Words, \
 I(TypeStruct,3), LinearBuffer, Words, \
 I(TypeStruct,13), PushBlock, UInt, UInt, UInt, UInt, UInt, UInt, UInt, UInt, UInt, UInt, UInt, \
 I(TypePointer,4), BufferPtr, SpvStorageClassUniform, Buffer, \
 I(TypePointer,4), LinearBufferPtr, SpvStorageClassUniform, LinearBuffer, \
 I(TypePointer,4), WordPtr, SpvStorageClassUniform, UInt, \
 I(TypePointer,4), PushBlockPtr, SpvStorageClassPushConstant, PushBlock, \
 I(TypePointer,4), PushPtr, SpvStorageClassPushConstant, UInt, \
 I(TypePointer,4), InputPtr, SpvStorageClassInput, UVec3, \
 I(Constant,4), UInt, C0, 0, \
 I(Constant,4), UInt, C1, 1, \
 I(Constant,4), UInt, C2, 2, \
 I(Constant,4), UInt, C3, 3, \
 I(Constant,4), UInt, C4, 4, \
 I(Constant,4), UInt, C5, 5, \
 I(Constant,4), UInt, C6, 6, \
 I(Constant,4), UInt, C7, 7, \
 I(Constant,4), UInt, C8, 8, \
 I(Constant,4), UInt, C9, 9, \
 I(Constant,4), UInt, C10, 10, \
 I(Constant,4), UInt, C16, 16, \
 I(Constant,4), UInt, C255, 255, \
 I(Constant,4), UInt, C65535, 65535, \
 \
 I(Variable,4), BufferPtr, Vram, SpvStorageClassUniform, \
 I(Variable,4), LinearBufferPtr, Linear, SpvStorageClassUniform, \
 I(Variable,4), PushBlockPtr, PC, SpvStorageClassPushConstant, \
 I(Variable,4), InputPtr, Global, SpvStorageClassInput,

/* Spread/compact a coordinate through the low set bit of each mask lane. */
#define MAP(TEST,SELECT) \
I(Function,5), UInt, Map, SpvFunctionControlMaskNone, MapType, \
 I(FunctionParameter,3), UInt, MapValue, \
 I(FunctionParameter,3), UInt, MapMask, \
 I(Label,2), MapEntry, \
 I(Branch,2), MapLoop, \
 I(Label,2), MapLoop, \
 I(Phi,7), UInt, Mask, MapMask, MapEntry, NextMask, MapContinue, \
 I(Phi,7), UInt, Bit, C1, MapEntry, NextBit, MapContinue, \
 I(Phi,7), UInt, Result, C0, MapEntry, NextResult, MapContinue, \
 I(INotEqual,5), Bool, HasMask, Mask, C0, \
 I(LoopMerge,4), MapDone, MapContinue, SpvLoopControlMaskNone, \
 I(BranchConditional,4), HasMask, MapBody, MapDone, \
 I(Label,2), MapBody, \
 I(ISub,5), UInt, NegativeMask, C0, Mask, \
 I(BitwiseAnd,5), UInt, Low, Mask, NegativeMask, \
 I(BitwiseAnd,5), UInt, TestBit, MapValue, TEST, \
 I(INotEqual,5), Bool, Set, TestBit, C0, \
 I(BitwiseOr,5), UInt, Selected, Result, SELECT, \
 I(Select,6), UInt, NextResult, Set, Selected, Result, \
 I(Not,4), UInt, NotLow, Low, \
 I(BitwiseAnd,5), UInt, NextMask, Mask, NotLow, \
 I(ShiftLeftLogical,5), UInt, NextBit, Bit, C1, \
 I(Branch,2), MapContinue, \
 I(Label,2), MapContinue, \
 I(Branch,2), MapLoop, \
 I(Label,2), MapDone, \
 I(ReturnValue,2), Result, \
 I(FunctionEnd,1),

/* One invocation assembles one linear output word, including palette lookup.
 * The final partial word is zero padded just as in the original kernel. */
static const uint32_t unswizzle[] = {
 SpvMagicNumber, 0x00010000, 0, Bound, 0,
 HEADER(Buffer,LinearBuffer)

 I(Function,5), Void, Main, SpvFunctionControlMaskNone, MainType,
 I(Label,2), Entry,
 I(Load,4), UVec3, Gid, Global,
 I(CompositeExtract,5), UInt, Index, Gid, 0,
 I(AccessChain,5), PushPtr, CountPtr, PC, C9,
 I(Load,4), UInt, Count, CountPtr,

 I(ULessThan,5), Bool, InBounds, Index, Count,
 I(SelectionMerge,3), Done, SpvSelectionControlMaskNone,
 I(BranchConditional,4), InBounds, Body, Done,
 I(Label,2), Body,
 I(AccessChain,5), PushPtr, DstPtr, PC, C1,
 I(Load,4), UInt, Dst, DstPtr,
 I(AccessChain,5), PushPtr, WidthPtr, PC, C2,
 I(Load,4), UInt, Width, WidthPtr,
 I(AccessChain,5), PushPtr, HeightPtr, PC, C3,
 I(Load,4), UInt, Height, HeightPtr,
 I(AccessChain,5), PushPtr, DepthPtr, PC, C4,
 I(Load,4), UInt, Depth, DepthPtr,
 I(AccessChain,5), PushPtr, BppPtr, PC, C5,
 I(Load,4), UInt, Bpp, BppPtr,
 I(AccessChain,5), PushPtr, PalettePtr, PC, C10,
 I(Load,4), UInt, Palette, PalettePtr,

 I(INotEqual,5), Bool, Paletted, Palette, C0,
 I(Select,6), UInt, OutputBpp, Paletted, C4, Bpp,
 I(UDiv,5), UInt, PerWord, C4, OutputBpp,
 I(IMul,5), UInt, First, Index, PerWord,
 I(IMul,5), UInt, PlaneSize, Width, Height,
 I(IMul,5), UInt, Texels, PlaneSize, Depth,
 I(Branch,2), Loop,
 I(Label,2), Loop,
 I(Phi,7), UInt, K, C0, Body, NextK, Continue,
 I(Phi,7), UInt, R, C0, Body, NextR, Continue,
 I(ULessThan,5), Bool, More, K, PerWord,
 I(LoopMerge,4), LoopDone, Continue, SpvLoopControlMaskNone,
 I(BranchConditional,4), More, LoopBody, LoopDone,
 I(Label,2), LoopBody,
 I(IAdd,5), UInt, Texel, First, K,
 I(ULessThan,5), Bool, ValidTexel, Texel, Texels,
 I(SelectionMerge,3), TailDone, SpvSelectionControlMaskNone,
 I(BranchConditional,4), ValidTexel, ReadTexel, TailDone,
 I(Label,2), ReadTexel,
 I(FunctionCall,5), UInt, FetchedValue, FetchTexel, Texel,
 I(IMul,5), UInt, LaneBytes, K, OutputBpp,
 I(IMul,5), UInt, Shift, LaneBytes, C8,
 I(ShiftLeftLogical,5), UInt, ShiftedValue, FetchedValue, Shift,
 I(BitwiseOr,5), UInt, Combined, R, ShiftedValue,
 I(Branch,2), TailDone,
 I(Label,2), TailDone,
 I(Phi,7), UInt, NextR, R, LoopBody, Combined, ReadTexel,
 I(Branch,2), Continue,
 I(Label,2), Continue,
 I(IAdd,5), UInt, NextK, K, C1,
 I(Branch,2), Loop,
 I(Label,2), LoopDone,
 I(ShiftRightLogical,5), UInt, DstWord, Dst, C2,
 I(IAdd,5), UInt, OutputIndex, DstWord, Index,
 I(AccessChain,6), WordPtr, OutputPtr, Linear, C0, OutputIndex,
 I(Store,3), OutputPtr, R,
 I(Branch,2), Done,
 I(Label,2), Done,
 I(Return,1),
 I(FunctionEnd,1),
 MAP(Bit,Low)
 /* Preserve a separate texel-lookup function rather than expanding its
  * nested format/palette selections inside the word-assembly loop. */
 I(Function,5), UInt, FetchTexel, SpvFunctionControlMaskNone, FetchType,
 I(FunctionParameter,3), UInt, TTexel,
 I(Label,2), FetchEntry,
 I(AccessChain,5), PushPtr, TSrcPtr, PC, C0,
 I(Load,4), UInt, TSrc, TSrcPtr,
 I(AccessChain,5), PushPtr, TWidthPtr, PC, C2,
 I(Load,4), UInt, TWidth, TWidthPtr,
 I(AccessChain,5), PushPtr, THeightPtr, PC, C3,
 I(Load,4), UInt, THeight, THeightPtr,
 I(AccessChain,5), PushPtr, TBppPtr, PC, C5,
 I(Load,4), UInt, TBpp, TBppPtr,
 I(AccessChain,5), PushPtr, TMaskXPtr, PC, C6,
 I(Load,4), UInt, TMaskX, TMaskXPtr,
 I(AccessChain,5), PushPtr, TMaskYPtr, PC, C7,
 I(Load,4), UInt, TMaskY, TMaskYPtr,
 I(AccessChain,5), PushPtr, TMaskZPtr, PC, C8,
 I(Load,4), UInt, TMaskZ, TMaskZPtr,
 I(AccessChain,5), PushPtr, TPalettePtr, PC, C10,
 I(Load,4), UInt, TPalette, TPalettePtr,
 I(IMul,5), UInt, TPlaneSize, TWidth, THeight,
 I(INotEqual,5), Bool, TPaletted, TPalette, C0,
 I(UMod,5), UInt, X, TTexel, TWidth,
 I(UDiv,5), UInt, Row, TTexel, TWidth,
 I(UMod,5), UInt, Y, Row, THeight,
 I(UDiv,5), UInt, Z, TTexel, TPlaneSize,
 I(FunctionCall,6), UInt, SX, Map, X, TMaskX,
 I(FunctionCall,6), UInt, SY, Map, Y, TMaskY,
 I(FunctionCall,6), UInt, SZ, Map, Z, TMaskZ,
 I(BitwiseOr,5), UInt, SXY, SX, SY,
 I(BitwiseOr,5), UInt, Swizzled, SXY, SZ,
 I(IMul,5), UInt, TexelBytes, Swizzled, TBpp,
 I(IAdd,5), UInt, Offset, TSrc, TexelBytes,
 I(ShiftRightLogical,5), UInt, WordIndex, Offset, C2,
 I(AccessChain,6), WordPtr, SourcePtr, Vram, C0, WordIndex,
 I(Load,4), UInt, Word, SourcePtr,
 I(IEqual,5), Bool, IsWord, TBpp, C4,
 I(SelectionMerge,3), ValueDone, SpvSelectionControlMaskNone,
 I(BranchConditional,4), IsWord, WholeWord, SmallValue,
 I(Label,2), WholeWord,
 I(Branch,2), ValueDone,
 I(Label,2), SmallValue,
 I(IEqual,5), Bool, IsHalf, TBpp, C2,
 I(SelectionMerge,3), SmallDone, SpvSelectionControlMaskNone,
 I(BranchConditional,4), IsHalf, HalfValue, ByteValue,
 I(Label,2), HalfValue,
 I(BitwiseAnd,5), UInt, HalfLane, Offset, C2,
 I(IMul,5), UInt, HalfShift, HalfLane, C8,
 I(ShiftRightLogical,5), UInt, HalfBits, Word, HalfShift,
 I(BitwiseAnd,5), UInt, Half, HalfBits, C65535,
 I(Branch,2), SmallDone,
 I(Label,2), ByteValue,
 I(BitwiseAnd,5), UInt, ByteLane, Offset, C3,
 I(IMul,5), UInt, ByteShift, ByteLane, C8,
 I(ShiftRightLogical,5), UInt, ByteBits, Word, ByteShift,
 I(BitwiseAnd,5), UInt, Byte, ByteBits, C255,
 I(SelectionMerge,3), PaletteDone, SpvSelectionControlMaskNone,
 I(BranchConditional,4), TPaletted, ReadPalette, PaletteDone,
 I(Label,2), ReadPalette,
 I(ShiftRightLogical,5), UInt, PaletteBase, TPalette, C2,
 I(IAdd,5), UInt, PaletteIndex, PaletteBase, Byte,
 I(AccessChain,6), WordPtr, PaletteDataPtr, Vram, C0, PaletteIndex,
 I(Load,4), UInt, Color, PaletteDataPtr,
 I(Branch,2), PaletteDone,
 I(Label,2), PaletteDone,
 I(Phi,7), UInt, ByteOrColor, Byte, ByteValue, Color, ReadPalette,
 I(Branch,2), SmallDone,
 I(Label,2), SmallDone,
 I(Phi,7), UInt, Small, Half, HalfValue, ByteOrColor, PaletteDone,
 I(Branch,2), ValueDone,
 I(Label,2), ValueDone,
 I(Phi,7), UInt, Value, Word, WholeWord, Small, SmallDone,
 I(ReturnValue,2), Value,
 I(FunctionEnd,1),
};
/* One invocation assembles one Morton-order word in guest RAM. The host
 * admits only complete 2D surfaces with 2- or 4-byte texels here. */
static const uint32_t swizzle[] = {
 SpvMagicNumber, 0x00010000, 0, Bound, 0,
 HEADER(LinearBuffer,Buffer)

 I(Function,5), Void, Main, SpvFunctionControlMaskNone, MainType,
 I(Label,2), Entry,
 I(Load,4), UVec3, Gid, Global,
 I(CompositeExtract,5), UInt, Index, Gid, 0,
 I(AccessChain,5), PushPtr, CountPtr, PC, C9,
 I(Load,4), UInt, Count, CountPtr,

 I(ULessThan,5), Bool, InBounds, Index, Count,
 I(SelectionMerge,3), Done, SpvSelectionControlMaskNone,
 I(BranchConditional,4), InBounds, Body, Done,
 I(Label,2), Body,
 I(AccessChain,5), PushPtr, SrcPtr, PC, C0,
 I(Load,4), UInt, Src, SrcPtr,
 I(AccessChain,5), PushPtr, DstPtr, PC, C1,
 I(Load,4), UInt, Dst, DstPtr,
 I(AccessChain,5), PushPtr, WidthPtr, PC, C2,
 I(Load,4), UInt, Width, WidthPtr,
 I(AccessChain,5), PushPtr, BppPtr, PC, C5,
 I(Load,4), UInt, Bpp, BppPtr,
 I(AccessChain,5), PushPtr, MaskXPtr, PC, C6,
 I(Load,4), UInt, MaskX, MaskXPtr,
 I(AccessChain,5), PushPtr, MaskYPtr, PC, C7,
 I(Load,4), UInt, MaskY, MaskYPtr,

 I(UDiv,5), UInt, PerWord, C4, Bpp,
 I(IMul,5), UInt, First, Index, PerWord,
 I(Branch,2), Loop,
 I(Label,2), Loop,
 I(Phi,7), UInt, K, C0, Body, NextK, Continue,
 I(Phi,7), UInt, R, C0, Body, NextR, Continue,
 I(ULessThan,5), Bool, More, K, PerWord,
 I(LoopMerge,4), LoopDone, Continue, SpvLoopControlMaskNone,
 I(BranchConditional,4), More, LoopBody, LoopDone,
 I(Label,2), LoopBody,
 I(IAdd,5), UInt, Texel, First, K,
 I(FunctionCall,6), UInt, X, Map, Texel, MaskX,
 I(FunctionCall,6), UInt, Y, Map, Texel, MaskY,
 I(IMul,5), UInt, Row, Y, Width,
 I(IAdd,5), UInt, LinearTexel, Row, X,
 I(IMul,5), UInt, TexelBytes, LinearTexel, Bpp,
 I(IAdd,5), UInt, Offset, Src, TexelBytes,
 I(ShiftRightLogical,5), UInt, WordIndex, Offset, C2,
 I(AccessChain,6), WordPtr, SourcePtr, Linear, C0, WordIndex,
 I(Load,4), UInt, Word, SourcePtr,
 I(IEqual,5), Bool, IsWord, Bpp, C4,
 I(BitwiseAnd,5), UInt, HalfLane, Offset, C2,
 I(IMul,5), UInt, HalfShift, HalfLane, C8,
 I(ShiftRightLogical,5), UInt, HalfBits, Word, HalfShift,
 I(BitwiseAnd,5), UInt, Half, HalfBits, C65535,
 I(Select,6), UInt, Value, IsWord, Word, Half,
 I(IMul,5), UInt, LaneBytes, K, Bpp,
 I(IMul,5), UInt, Shift, LaneBytes, C8,
 I(ShiftLeftLogical,5), UInt, ShiftedValue, Value, Shift,
 I(BitwiseOr,5), UInt, NextR, R, ShiftedValue,
 I(Branch,2), Continue,
 I(Label,2), Continue,
 I(IAdd,5), UInt, NextK, K, C1,
 I(Branch,2), Loop,
 I(Label,2), LoopDone,
 I(ShiftRightLogical,5), UInt, DstWord, Dst, C2,
 I(IAdd,5), UInt, OutputIndex, DstWord, Index,
 I(AccessChain,6), WordPtr, OutputPtr, Vram, C0, OutputIndex,
 I(Store,3), OutputPtr, R,
 I(Branch,2), Done,
 I(Label,2), Done,
 I(Return,1),
 I(FunctionEnd,1),
 MAP(Low,Bit)
};

SpvStaticModule pgraph_spv_swizzle(bool to_ram)
{
 return to_ram ? (SpvStaticModule){swizzle, sizeof(swizzle)/sizeof(*swizzle)}
               : (SpvStaticModule){unswizzle, sizeof(unswizzle)/sizeof(*unswizzle)};
}
