// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2015 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <utility>
#include <glad/glad.h>
#include "common/common_funcs.h"

namespace OpenGL {

class OGLRenderbuffer final : Common::NonCopyable {
public:
    OGLRenderbuffer() = default;

    OGLRenderbuffer(OGLRenderbuffer&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLRenderbuffer() {
        Release();
    }

    OGLRenderbuffer& operator=(OGLRenderbuffer&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

class OGLTexture final : Common::NonCopyable {
public:
    OGLTexture() = default;

    OGLTexture(OGLTexture&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLTexture() {
        Release();
    }

    OGLTexture& operator=(OGLTexture&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create(GLenum target);

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

class OGLTextureView final : Common::NonCopyable {
public:
    OGLTextureView() = default;

    OGLTextureView(OGLTextureView&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLTextureView() {
        Release();
    }

    OGLTextureView& operator=(OGLTextureView&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

class OGLSampler final : Common::NonCopyable {
public:
    OGLSampler() = default;

    OGLSampler(OGLSampler&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLSampler() {
        Release();
    }

    OGLSampler& operator=(OGLSampler&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

class OGLShader final : Common::NonCopyable {
public:
    OGLShader() = default;

    OGLShader(OGLShader&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLShader() {
        Release();
    }

    OGLShader& operator=(OGLShader&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    void Release();

    GLuint handle = 0;
};

class OGLProgram final : Common::NonCopyable {
public:
    OGLProgram() = default;

    OGLProgram(OGLProgram&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLProgram() {
        Release();
    }

    OGLProgram& operator=(OGLProgram&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

class OGLAssemblyProgram final : Common::NonCopyable {
public:
    OGLAssemblyProgram() = default;

    OGLAssemblyProgram(OGLAssemblyProgram&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLAssemblyProgram() {
        Release();
    }

    OGLAssemblyProgram& operator=(OGLAssemblyProgram&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

class OGLPipeline final : Common::NonCopyable {
public:
    OGLPipeline() = default;
    OGLPipeline(OGLPipeline&& o) noexcept : handle{std::exchange<GLuint>(o.handle, 0)} {}

    ~OGLPipeline() {
        Release();
    }
    OGLPipeline& operator=(OGLPipeline&& o) noexcept {
        handle = std::exchange<GLuint>(o.handle, 0);
        return *this;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

class OGLBuffer final : Common::NonCopyable {
public:
    OGLBuffer() = default;

    OGLBuffer(OGLBuffer&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLBuffer() {
        Release();
    }

    OGLBuffer& operator=(OGLBuffer&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

class OGLSync final : Common::NonCopyable {
public:
    OGLSync() = default;

    OGLSync(OGLSync&& o) noexcept : handle(std::exchange(o.handle, nullptr)) {}

    ~OGLSync() {
        Release();
    }
    OGLSync& operator=(OGLSync&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, nullptr);
        return *this;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

    /// Checks if the sync has been signaled
    bool IsSignaled() const noexcept;

    GLsync handle = 0;
};

class OGLFramebuffer final : Common::NonCopyable {
public:
    OGLFramebuffer() = default;

    OGLFramebuffer(OGLFramebuffer&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLFramebuffer() {
        Release();
    }

    OGLFramebuffer& operator=(OGLFramebuffer&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

class OGLQuery final : Common::NonCopyable {
public:
    OGLQuery() = default;

    OGLQuery(OGLQuery&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLQuery() {
        Release();
    }

    OGLQuery& operator=(OGLQuery&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create(GLenum target);

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

class OGLTransformFeedback final : Common::NonCopyable {
public:
    OGLTransformFeedback() = default;

    OGLTransformFeedback(OGLTransformFeedback&& o) noexcept : handle(std::exchange(o.handle, 0)) {}

    ~OGLTransformFeedback() {
        Release();
    }

    OGLTransformFeedback& operator=(OGLTransformFeedback&& o) noexcept {
        Release();
        handle = std::exchange(o.handle, 0);
        return *this;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

    GLuint handle = 0;
};

} // namespace OpenGL
