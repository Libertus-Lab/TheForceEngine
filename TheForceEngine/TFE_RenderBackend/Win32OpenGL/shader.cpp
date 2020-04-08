#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <GL/glew.h>
#include <vector>

namespace ShaderGL
{
	static const char* c_shaderAttrName[]=
	{
		"vtx_pos",   // ATTR_POS
		"vtx_nrm",   // ATTR_NRM
		"vtx_uv",    // ATTR_UV
		"vtx_uv1",   // ATTR_UV1
		"vtx_uv2",   // ATTR_UV2
		"vtx_uv3",   // ATTR_UV3
		"vtx_color", // ATTR_COLOR
	};

	static const s32 c_glslVersion = 130;
	static const GLchar* c_glslVersionString = "#version 130\n";
	static std::vector<char> s_buffers[2];

	// If you get an error please report on github. You may try different GL context version or GLSL version. See GL<>GLSL version table at the top of this file.
	bool CheckShader(GLuint handle, const char* desc)
	{
		GLint status = 0, log_length = 0;
		glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
		glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_length);
		if ((GLboolean)status == GL_FALSE)
		{
			TFE_System::logWrite(LOG_ERROR, "Shader", "Failed to compile %s!\n", desc);
		}

		if (log_length > 1)
		{
			std::vector<char> buf;
			buf.resize(size_t(log_length + 1));
			glGetShaderInfoLog(handle, log_length, NULL, (GLchar*)buf.data());
			TFE_System::logWrite(LOG_ERROR, "Shader", "Error: %s\n", buf.data());
		}
		return (GLboolean)status == GL_TRUE;
	}

	// If you get an error please report on GitHub. You may try different GL context version or GLSL version.
	bool CheckProgram(GLuint handle, const char* desc)
	{
		GLint status = 0, log_length = 0;
		glGetProgramiv(handle, GL_LINK_STATUS, &status);
		glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &log_length);
		if ((GLboolean)status == GL_FALSE)
		{
			TFE_System::logWrite(LOG_ERROR, "Shader", "Failed to link %s! (with GLSL '%s')\n", desc, c_glslVersionString);
		}

		if (log_length > 1)
		{
			std::vector<char> buf;
			buf.resize(size_t(log_length + 1));
			glGetShaderInfoLog(handle, log_length, NULL, (GLchar*)buf.data());
			TFE_System::logWrite(LOG_ERROR, "Shader", "Error: %s\n", buf.data());
		}
		return (GLboolean)status == GL_TRUE;
	}
}

bool Shader::create(const char* vertexShaderGLSL, const char* fragmentShaderGLSL)
{
	// Create shaders
	const GLchar* vertex_shader_with_version[2] = { ShaderGL::c_glslVersionString, vertexShaderGLSL };
	u32 vertHandle = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertHandle, 2, vertex_shader_with_version, NULL);
	glCompileShader(vertHandle);
	if (!ShaderGL::CheckShader(vertHandle, "vertex shader")) { return false; }

	const GLchar* fragment_shader_with_version[2] = { ShaderGL::c_glslVersionString, fragmentShaderGLSL };
	u32 fragHandle = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragHandle, 2, fragment_shader_with_version, NULL);
	glCompileShader(fragHandle);
	if (!ShaderGL::CheckShader(fragHandle, "fragment shader")) { return false; }

	m_gpuHandle = glCreateProgram();
	glAttachShader(m_gpuHandle, vertHandle);
	glAttachShader(m_gpuHandle, fragHandle);
	// Bind vertex attribute names to slots.
	for (u32 i = 0; i < ATTR_COUNT; i++)
	{
		glBindAttribLocation(m_gpuHandle, i, ShaderGL::c_shaderAttrName[i]);
	}
	glLinkProgram(m_gpuHandle);
	if (!ShaderGL::CheckProgram(m_gpuHandle, "shader program")) { return false; }

	return m_gpuHandle != 0;
}

bool Shader::load(const char* vertexShaderFile, const char* fragmentShaderFile)
{
	char vtxPath[TFE_MAX_PATH];
	char frgPath[TFE_MAX_PATH];
	TFE_Paths::appendPath(PATH_PROGRAM, vertexShaderFile, vtxPath);
	TFE_Paths::appendPath(PATH_PROGRAM, fragmentShaderFile, frgPath);

	FileStream file;
	if (!file.open(vtxPath, FileStream::MODE_READ)) { return false; }
	const u32 vtxSize = (u32)file.getSize();
	ShaderGL::s_buffers[0].resize(vtxSize+1);
	file.readBuffer(ShaderGL::s_buffers[0].data(), vtxSize);
	ShaderGL::s_buffers[0].data()[vtxSize] = 0;
	file.close();

	if (!file.open(frgPath, FileStream::MODE_READ)) { return false; }
	const u32 fragSize = (u32)file.getSize();
	ShaderGL::s_buffers[1].resize(fragSize+1);
	file.readBuffer(ShaderGL::s_buffers[1].data(), fragSize);
	ShaderGL::s_buffers[1].data()[fragSize] = 0;
	file.close();

	return create(ShaderGL::s_buffers[0].data(), ShaderGL::s_buffers[1].data());
}

void Shader::destroy()
{
	glDeleteProgram(m_gpuHandle);
	m_gpuHandle = 0;
}

void Shader::bind()
{
	glUseProgram(m_gpuHandle);
}

void Shader::unbind()
{
	glUseProgram(0);
}

s32 Shader::getVariableId(const char* name)
{
	return glGetUniformLocation(m_gpuHandle, name);
}

void Shader::bindTextureNameToSlot(const char* texName, s32 slot)
{
	const s32 curSlot = glGetUniformLocation(m_gpuHandle, texName);
	bind();
	glUniform1i(curSlot, slot);
	unbind();
}

void Shader::setVariable(s32 id, ShaderVariableType type, const f32* data)
{
	if (id < 0) { return; }

	switch (type)
	{
	case SVT_SCALAR:
		glUniform1f(id, data[0]);
		break;
	case SVT_VEC2:
		glUniform2fv(id, 1, data);
		break;
	case SVT_VEC3:
		glUniform3fv(id, 1, data);
		break;
	case SVT_VEC4:
		glUniform4fv(id, 1, data);
		break;
	case SVT_MAT3x3:
		glUniformMatrix3fv(id, 1, false, data);
		break;
	case SVT_MAT4x3:
		glUniformMatrix4x3fv(id, 1, false, data);
		break;
	case SVT_MAT4x4:
		glUniformMatrix4fv(id, 1, false, data);
		break;
	}
}