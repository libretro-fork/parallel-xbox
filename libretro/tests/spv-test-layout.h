/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only assertions for fixed kernels, not a general reflection parser.
 * spirv-val independently validates the complete saved modules. */
#include <spirv/unified1/spirv.h>

static const uint32_t *spv_test_definition(const uint32_t *words, size_t count,
                                         SpvOp opcode, uint32_t id)
{
    for (size_t i = 5; i < count;) {
        unsigned length = words[i] >> 16;
        assert(length && length <= count - i);
        if ((words[i] & 0xffff) == (unsigned)opcode && words[i + 1] == id) return words + i;
        i += length;
    }
    assert(!"missing SPIR-V definition");
    return NULL;
}

static uint32_t spv_test_decoration(const uint32_t *words, size_t count,
                                   SpvOp opcode, uint32_t id, uint32_t member,
                                   SpvDecoration decoration)
{
    for (size_t i = 5; i < count;) {
        unsigned length = words[i] >> 16;
        assert(length && length <= count - i);
        if ((words[i] & 0xffff) == (unsigned)opcode && words[i + 1] == id) {
            unsigned at = 2;
            if (opcode == SpvOpMemberDecorate) {
                if (words[i + at++] != member) { i += length; continue; }
            }
            if (words[i + at++] == (unsigned)decoration) return at < length ? words[i + at] : 1;
        }
        i += length;
    }
    assert(!"missing SPIR-V decoration");
    return 0;
}

static void spv_test_layout(const uint32_t *words, size_t count,
                            unsigned buffers, unsigned push_members,
                            unsigned workgroup)
{
    assert(count >= 5 && words[0] == SpvMagicNumber);
    unsigned found_buffers = 0, found_push = 0, found_group = 0, bindings = 0;
    for (size_t i = 5; i < count;) {
        unsigned length = words[i] >> 16, opcode = words[i] & 0xffff;
        assert(length && length <= count - i);
        if (opcode == SpvOpExecutionMode && words[i + 2] == SpvExecutionModeLocalSize) {
            assert(length == 6 && words[i + 3] == workgroup && words[i + 4] == 1 && words[i + 5] == 1);
            found_group++;
        }
        if (opcode == SpvOpVariable) {
            uint32_t id = words[i + 2], storage = words[i + 3];
            assert(storage != SpvStorageClassUniformConstant);
            if (storage == SpvStorageClassUniform || storage == SpvStorageClassStorageBuffer || storage == SpvStorageClassPushConstant) {
                const uint32_t *pointer = spv_test_definition(words,count,SpvOpTypePointer,words[i + 1]);
                assert(pointer[2] == storage);
                const uint32_t *block = spv_test_definition(words,count,SpvOpTypeStruct,pointer[3]);
                if (storage == SpvStorageClassPushConstant) {
                    found_push++;
                    assert((block[0] >> 16) == push_members + 2);
                    for (unsigned j = 0; j < push_members; j++) {
                        assert(spv_test_decoration(words,count,SpvOpMemberDecorate,block[1],j,SpvDecorationOffset) == j * 4);
                        const uint32_t *type = spv_test_definition(words,count,SpvOpTypeInt,block[j + 2]);
                        assert(type[2] == 32 && type[3] == 0);
                    }
                } else {
                    assert(spv_test_decoration(words,count,SpvOpDecorate,id,0,SpvDecorationDescriptorSet) == 0);
                    unsigned binding = spv_test_decoration(words,count,SpvOpDecorate,id,0,SpvDecorationBinding);
                    assert(binding < buffers && !(bindings & (1u << binding)));
                    bindings |= 1u << binding;
                    assert(spv_test_decoration(words,count,SpvOpDecorate,block[1],0,
                           storage == SpvStorageClassUniform ? SpvDecorationBufferBlock : SpvDecorationBlock));
                    found_buffers++;
                }
            }
        }
        i += length;
    }
    assert(found_buffers == buffers && found_push == (push_members != 0));
    assert(found_group == (workgroup != 0));
}
