/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/vk/GrVkCommandPool.h"

#include "src/gpu/GrContextPriv.h"
#include "src/gpu/vk/GrVkCommandBuffer.h"
#include "src/gpu/vk/GrVkGpu.h"

GrVkCommandPool* GrVkCommandPool::Create(GrVkGpu* gpu) {
    VkCommandPoolCreateFlags cmdPoolCreateFlags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (gpu->protectedContext()) {
        cmdPoolCreateFlags |= VK_COMMAND_POOL_CREATE_PROTECTED_BIT;
    }

    const VkCommandPoolCreateInfo cmdPoolInfo = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,  // sType
        nullptr,                                     // pNext
        cmdPoolCreateFlags,                          // CmdPoolCreateFlags
        gpu->queueIndex(),                           // queueFamilyIndex
    };
    VkResult result;
    VkCommandPool pool;
    GR_VK_CALL_RESULT(gpu, result, CreateCommandPool(gpu->device(), &cmdPoolInfo, nullptr, &pool));
    if (result != VK_SUCCESS) {
        return nullptr;
    }
    return new GrVkCommandPool(gpu, pool);
}

GrVkCommandPool::GrVkCommandPool(const GrVkGpu* gpu, VkCommandPool commandPool)
        : fCommandPool(commandPool) {
    fPrimaryCommandBuffer.reset(GrVkPrimaryCommandBuffer::Create(gpu, this));
}

std::unique_ptr<GrVkSecondaryCommandBuffer> GrVkCommandPool::findOrCreateSecondaryCommandBuffer(
        GrVkGpu* gpu) {
    std::unique_ptr<GrVkSecondaryCommandBuffer> result;
    if (fAvailableSecondaryBuffers.count()) {
        result = std::move(fAvailableSecondaryBuffers.back());
        fAvailableSecondaryBuffers.pop_back();
    } else{
        result.reset(GrVkSecondaryCommandBuffer::Create(gpu, this));
    }
    return result;
}

void GrVkCommandPool::recycleSecondaryCommandBuffer(GrVkSecondaryCommandBuffer* buffer) {
    SkASSERT(buffer->commandPool() == this);
    std::unique_ptr<GrVkSecondaryCommandBuffer> scb(buffer);
    fAvailableSecondaryBuffers.push_back(std::move(scb));
}

void GrVkCommandPool::close() {
    fOpen = false;
}

void GrVkCommandPool::reset(GrVkGpu* gpu) {
    SkASSERT(!fOpen);
    fOpen = true;
    fPrimaryCommandBuffer->recycleSecondaryCommandBuffers(gpu);
    // We can't use the normal result macro calls here because we may call reset on a different
    // thread and we can't be modifying the lost state on the GrVkGpu. We just call
    // vkResetCommandPool and assume the "next" vulkan call will catch the lost device.
    SkDEBUGCODE(VkResult result = )GR_VK_CALL(gpu->vkInterface(),
                                              ResetCommandPool(gpu->device(), fCommandPool, 0));
    SkASSERT(result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST);
}

void GrVkCommandPool::releaseResources(GrVkGpu* gpu) {
    TRACE_EVENT0("skia.gpu", TRACE_FUNC);
    SkASSERT(!fOpen);
    fPrimaryCommandBuffer->releaseResources(gpu);
}

void GrVkCommandPool::abandonGPUData() const {
    fPrimaryCommandBuffer->abandonGPUData();
    for (const auto& buffer : fAvailableSecondaryBuffers) {
        buffer->abandonGPUData();
    }
}

void GrVkCommandPool::freeGPUData(GrVkGpu* gpu) const {
    fPrimaryCommandBuffer->freeGPUData(gpu);
    for (const auto& buffer : fAvailableSecondaryBuffers) {
        buffer->freeGPUData(gpu);
    }
    if (fCommandPool != VK_NULL_HANDLE) {
        GR_VK_CALL(gpu->vkInterface(),
                   DestroyCommandPool(gpu->device(), fCommandPool, nullptr));
    }
}
