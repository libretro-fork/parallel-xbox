if vulkan.found()

# The GLSL to SPIR-V compiler, the two Vulkan renderers' (the SPIR-V header
# comes with SPIRV-Reflect)
glslc = declare_dependency(
	include_directories: include_directories('../../../../../subprojects/SPIRV-Reflect/include'))
specific_ss.add([glslc, files('glslc.c', 'glslc_lex.c')])

endif
