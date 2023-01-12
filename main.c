/*
 * Copyright (c) 2069 Sir Cumsalot <cums@alot.com>
 * Copyright (c) 2012 Arvin Schnell <arvin.schnell@gmail.com>
 * Copyright (c) 2012 Rob Clark <rob@ti.com>
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* Based on kmscube example written by Rob Clark, based on test app originally
 * written by Arvin Schnell.
 *
 * Compile and run this with minigbm:
 *
 *   https://chromium.googlesource.com/chromiumos/platform/minigbm
 *
 * Edit the minigbm Makefile to add -DGBM_I915 to CPPFLAGS, then compile and
 * install with make DESTDIR=<some path>. Then pass --with-minigbm=<some path>
 * to configure when configuring vkcube
 */

#define _DEFAULT_SOURCE /* for major() */


#include "cube.h"

enum display_mode {
   DISPLAY_MODE_AUTO = 0,
   DISPLAY_MODE_HEADLESS,
   DISPLAY_MODE_KMS,
   DISPLAY_MODE_XCB,
   DISPLAY_MODE_KHR,
};

static enum display_mode display_mode = DISPLAY_MODE_XCB;
static uint32_t width = 1024, height = 768;
static const char *arg_out_file = "./cube.png";
static bool protected_chain = false;

void
failv(const char *format, va_list args)
{
	// vfprintf(stderr, format, args);
	// fprintf(stderr, "\n");
	// exit(1);
}

void
fail(const char* text)
{
	printf("epic fail %s", text);
	// va_list args;

	// // va_start(args, format);
	// failv(format, args);
	// va_end(args);
}

void
fail_if(int cond, const char *format, ...)
{
	// va_list args;

	// if (!cond)
	// {
	// 	return;
	// }

	// // va_start(args, format);
	// failv(format, args);
	// va_end(args);
}

static char *
xstrdup(const char *s)
{
	// char *dup = strdup(s);
	// if (!dup) 
	// {
	// 	fprintf(stderr, "out of memory\n");
	// 	abort();
	// }

	// return dup;
}

static int find_image_memory(struct vkcube *vc, unsigned allowed)
{
	VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | (vc->protected_en ? VK_MEMORY_PROPERTY_PROTECTED_BIT : 0);

	for (unsigned i = 0; (1u << i) <= allowed && i <= vc->memory_properties.memoryTypeCount; ++i) 
	{
		if ((allowed & (1u << i)) && (vc->memory_properties.memoryTypes[i].propertyFlags & flags))
		{
			return i;
		}
	}
	return -1;
}

static void
init_vk(struct vkcube *vc, const char *extension)
{
	VkResult res = vkCreateInstance(
		&(VkInstanceCreateInfo) 
		{
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = 
				&(VkApplicationInfo) 
				{
					.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
					.pApplicationName = "vkcube",
					.apiVersion = VK_MAKE_VERSION(1, 1, 0),
				},
			.enabledExtensionCount = extension ? 2 : 0,
			.ppEnabledExtensionNames = 
				(const char *[2]) 
				{
					VK_KHR_SURFACE_EXTENSION_NAME,
					extension,
				},
		},
		NULL,
		&vc->instance
	);

	fail_if(res != VK_SUCCESS, "Failed to create Vulkan instance.\n");

	uint32_t count;
	res = vkEnumeratePhysicalDevices(vc->instance, &count, NULL);
	fail_if(res != VK_SUCCESS || count == 0, "No Vulkan devices found.\n");
	VkPhysicalDevice pd[count];
	vkEnumeratePhysicalDevices(vc->instance, &count, pd);
	vc->physical_device = pd[1];
	printf("%d physical devices\n", count);

	VkPhysicalDeviceProtectedMemoryFeatures 
	protected_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES,
	};

	VkPhysicalDeviceFeatures2 
	features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &protected_features,
	};

	vkGetPhysicalDeviceFeatures2(vc->physical_device, &features);

	if (protected_chain && !protected_features.protectedMemory)
	{
		printf("Requested protected memory but not supported by device, dropping...\n");
	}
		
	vc->protected_en = protected_chain && protected_features.protectedMemory;

	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(vc->physical_device, &properties);
	printf("vendor id %04x, device name %s\n", properties.vendorID, properties.deviceName);

	vkGetPhysicalDeviceMemoryProperties(vc->physical_device, &vc->memory_properties);

	vkGetPhysicalDeviceQueueFamilyProperties(vc->physical_device, &count, NULL);
	assert(count > 0);
	VkQueueFamilyProperties props[count];
	vkGetPhysicalDeviceQueueFamilyProperties(vc->physical_device, &count, props);
	assert(props[0].queueFlags & VK_QUEUE_GRAPHICS_BIT);

	vkCreateDevice(
		vc->physical_device,
		&(VkDeviceCreateInfo) 
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = 
				&(VkDeviceQueueCreateInfo) 
				{
					.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					.queueFamilyIndex = 0,
					.queueCount = 1,
					.flags = vc->protected_en ? VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT : 0,
					.pQueuePriorities = (float []) { 1.0f },
				},
			.enabledExtensionCount = 1,
			.ppEnabledExtensionNames = 
				(const char * const []) 
				{
					VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				},
		},
		NULL,
		&vc->device
	);

	vkGetDeviceQueue2(
		vc->device, 
		&(VkDeviceQueueInfo2) 
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
			.flags = vc->protected_en ? VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT : 0,
			.queueFamilyIndex = 0,
			.queueIndex = 0,
		}, 
		&vc->queue
	);
}

static void
init_vk_objects(struct vkcube *vc)
{
	printf("vk creating render pass\n");
	vkCreateRenderPass(
		vc->device,
		&(VkRenderPassCreateInfo) 
		{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = 
				(VkAttachmentDescription[]) 
				{
					{
					.format = vc->image_format,
					.samples = 1,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
					}
				},
			.subpassCount = 1,
			.pSubpasses = 
				(VkSubpassDescription []) 
				{
					{
					.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
					.inputAttachmentCount = 0,
					.colorAttachmentCount = 1,
					.pColorAttachments = (VkAttachmentReference []) {
						{
							.attachment = 0,
							.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
						}
					},
					.pResolveAttachments = (VkAttachmentReference []) {
						{
							.attachment = VK_ATTACHMENT_UNUSED,
							.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
						}
					},
					.pDepthStencilAttachment = NULL,
					.preserveAttachmentCount = 0,
					.pPreserveAttachments = NULL,
					}
				},
			.dependencyCount = 0
		},
		NULL,
		&vc->render_pass
	);

	printf("done\n");
	printf("function ptr : %p\n", vc->model.init);

	// segfaults
	// vc->model.init(vc);
	init_cube(vc);

	printf("vk model initialized\n");

	vkCreateCommandPool(
		vc->device,
		&(const VkCommandPoolCreateInfo) 
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = 0,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
					(vc->protected_en ? VK_COMMAND_POOL_CREATE_PROTECTED_BIT : 0)
		},
		NULL,
		&vc->cmd_pool
	);

	printf("vk creating command pool\n");

	vkCreateSemaphore(
		vc->device,
		&(VkSemaphoreCreateInfo) 
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		},
		NULL,
		&vc->semaphore
	);

	printf("vk creating semaphore\n");
}

static void
init_buffer(struct vkcube *vc, struct vkcube_buffer *b)
{
	vkCreateImageView(
		vc->device,
		&(VkImageViewCreateInfo) 
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = b->image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = vc->image_format,
			.components = {
			.r = VK_COMPONENT_SWIZZLE_R,
			.g = VK_COMPONENT_SWIZZLE_G,
			.b = VK_COMPONENT_SWIZZLE_B,
			.a = VK_COMPONENT_SWIZZLE_A,
			},
			.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
			},
		},
		NULL,
		&b->view
	);

	vkCreateFramebuffer(
		vc->device,
		&(VkFramebufferCreateInfo) 
		{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = vc->render_pass,
			.attachmentCount = 1,
			.pAttachments = &b->view,
			.width = vc->width,
			.height = vc->height,
			.layers = 1
		},
		NULL,
		&b->framebuffer
	);

	vkCreateFence(
		vc->device,
		&(VkFenceCreateInfo) 
		{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT
		},
		NULL,
		&b->fence
	);

	vkAllocateCommandBuffers(
		vc->device,
		&(VkCommandBufferAllocateInfo) 
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = vc->cmd_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		},
		&b->cmd_buffer
	);
}

static void
write_buffer(struct vkcube *vc, struct vkcube_buffer *b)
{
	// const char *filename = arg_out_file;
	// uint32_t mem_size = b->stride * vc->height;
	// void *map;

	// vkMapMemory(vc->device, b->mem, 0, mem_size, 0, &map);

	// fprintf(stderr, "writing first frame to %s\n", filename);
	// // write_png(filename, vc->width, vc->height, b->stride, map);
}

/* Swapchain-based code - shared between XCB and Wayland */
static VkFormat
choose_surface_format(struct vkcube *vc)
{
	uint32_t num_formats = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(vc->physical_device, vc->surface, &num_formats, NULL);
	assert(num_formats > 0);

	VkSurfaceFormatKHR formats[num_formats];

	vkGetPhysicalDeviceSurfaceFormatsKHR(vc->physical_device, vc->surface, &num_formats, formats);

	VkFormat format = VK_FORMAT_UNDEFINED;
	for (int i = 0; i < num_formats; i++) 
	{
		switch (formats[i].format) 
		{
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_UNORM:
			/* These formats are all fine */
			format = formats[i].format;
			break;
		case VK_FORMAT_R8G8B8_SRGB:
		case VK_FORMAT_B8G8R8_SRGB:
		case VK_FORMAT_R8G8B8_UNORM:
		case VK_FORMAT_R5G6B5_UNORM_PACK16:
		case VK_FORMAT_B5G6R5_UNORM_PACK16:
			/* We would like to support these but they don't seem to work. */
		default:
			continue;
		}
	}

	assert(format != VK_FORMAT_UNDEFINED);

	return format;
}

static void
create_swapchain(struct vkcube *vc)
{
	VkSurfaceCapabilitiesKHR surface_caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vc->physical_device, vc->surface, &surface_caps);
	assert(surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);

	VkBool32 supported;
	vkGetPhysicalDeviceSurfaceSupportKHR(vc->physical_device, 0, vc->surface, &supported);
	assert(supported);

	uint32_t count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(vc->physical_device, vc->surface, &count, NULL);
	VkPresentModeKHR present_modes[count];
	vkGetPhysicalDeviceSurfacePresentModesKHR(vc->physical_device, vc->surface, &count, present_modes);
	int i;

	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

	for (i = 0; i < count; i++) 
	{
		if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) 
		{
			present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
			break;
		}
	}

	uint32_t minImageCount = 2;
	if (minImageCount < surface_caps.minImageCount) 
	{
		if (surface_caps.minImageCount > MAX_NUM_IMAGES)
		{
			fail("surface_caps.minImageCount is too large (is: , max: )");
		}

		minImageCount = surface_caps.minImageCount;
	}

	if (surface_caps.maxImageCount > 0 && minImageCount > surface_caps.maxImageCount) 
	{
		minImageCount = surface_caps.maxImageCount;
	}

	vkCreateSwapchainKHR(
		vc->device,
		&(VkSwapchainCreateInfoKHR) 
		{
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.flags = vc->protected_en ? VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR : 0,
			.surface = vc->surface,
			.minImageCount = minImageCount,
			.imageFormat = vc->image_format,
			.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
			.imageExtent = { vc->width, vc->height },
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 1,
			.pQueueFamilyIndices = (uint32_t[]) { 0 },
			.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
			.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode = present_mode,
		}, 
		NULL, 
		&vc->swap_chain
	);

	vkGetSwapchainImagesKHR(vc->device, vc->swap_chain, &vc->image_count, NULL);
	assert(vc->image_count > 0);
	VkImage swap_chain_images[vc->image_count];
	vkGetSwapchainImagesKHR(vc->device, vc->swap_chain, &vc->image_count, swap_chain_images);

	assert(vc->image_count <= MAX_NUM_IMAGES);
	for (uint32_t i = 0; i < vc->image_count; i++) 
	{
		vc->buffers[i].image = swap_chain_images[i];
		init_buffer(vc, &vc->buffers[i]);
	}
}

/* XCB display code - render to X window */
static xcb_atom_t
get_atom(struct xcb_connection_t *conn, const char *name)
{
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;
	xcb_atom_t atom;

	cookie = xcb_intern_atom(conn, 0, strlen(name), name);
	reply = xcb_intern_atom_reply(conn, cookie, NULL);

	if (reply)
	{
		atom = reply->atom;
	}
	else
	{
		atom = XCB_NONE;
	}

	free(reply);
	return atom;
}

// Return -1 on failure.
static int
init_xcb(struct vkcube *vc)
{
	xcb_screen_iterator_t iter;
	static const char title[] = "Vulkan Cube";

	vc->xcb.conn = xcb_connect(0, 0);
	if (xcb_connection_has_error(vc->xcb.conn))
	{
		return -1;
	}
	printf("xcb connection is ok\n");

	vc->xcb.window = xcb_generate_id(vc->xcb.conn);

	printf("xcb id generated\n");

	uint32_t window_values[] = {
		XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_KEY_PRESS
	};

	iter = xcb_setup_roots_iterator(xcb_get_setup(vc->xcb.conn));

	printf("xcb root iterator setup is ok\n");

	xcb_create_window(
		vc->xcb.conn,
		XCB_COPY_FROM_PARENT,
		vc->xcb.window,
		iter.data->root,
		0, 0,
		vc->width,
		vc->height,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		iter.data->root_visual,
		XCB_CW_EVENT_MASK, window_values
	);

	printf("xcb window is created\n");

	vc->xcb.atom_wm_protocols = get_atom(vc->xcb.conn, "WM_PROTOCOLS");
	vc->xcb.atom_wm_delete_window = get_atom(vc->xcb.conn, "WM_DELETE_WINDOW");
	xcb_change_property(
		vc->xcb.conn,
		XCB_PROP_MODE_REPLACE,
		vc->xcb.window,
		vc->xcb.atom_wm_protocols,
		XCB_ATOM_ATOM,
		32,
		1, &vc->xcb.atom_wm_delete_window
	);

	printf("xcb properties are changed\n");

	xcb_change_property(
		vc->xcb.conn,
		XCB_PROP_MODE_REPLACE,
		vc->xcb.window,
		get_atom(vc->xcb.conn, "_NET_WM_NAME"),
		get_atom(vc->xcb.conn, "UTF8_STRING"),
		8, // sizeof(char),
		strlen(title), title
	);

	printf("xcb properties are changed\n");

	xcb_map_window(vc->xcb.conn, vc->xcb.window);

	printf("xcb window is mapped\n");

	xcb_flush(vc->xcb.conn);

	printf("xcb flushed\n");

	init_vk(vc, VK_KHR_XCB_SURFACE_EXTENSION_NAME);

	printf("xcb vk intialized\n");

	PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR get_xcb_presentation_support = 
		(PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR) 
		vkGetInstanceProcAddr(vc->instance, "vkGetPhysicalDeviceXcbPresentationSupportKHR");

	PFN_vkCreateXcbSurfaceKHR create_xcb_surface = 
		(PFN_vkCreateXcbSurfaceKHR) 
		vkGetInstanceProcAddr(vc->instance, "vkCreateXcbSurfaceKHR");

	printf("vk pfn function calls passed\n");

	if (
		!get_xcb_presentation_support(
			vc->physical_device, 0,
			vc->xcb.conn,
			iter.data->root_visual
		)
	) 
	{
		fail("Vulkan not supported on given X window");
	}

	create_xcb_surface(
		vc->instance,
		&(VkXcbSurfaceCreateInfoKHR) 
		{
			.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
			.connection = vc->xcb.conn,
			.window = vc->xcb.window,
		}, 
		NULL, 
		&vc->surface
	);

	printf("xcb surface created\n");

	vc->image_format = choose_surface_format(vc);

	printf("xcb surface format chosen\n");

	init_vk_objects(vc);

	printf("xcb vk objects initialized\n");

	vc->image_count = 0;

	return 0;
}

static void
schedule_xcb_repaint(struct vkcube *vc)
{
	xcb_client_message_event_t client_message;

	client_message.response_type = XCB_CLIENT_MESSAGE;
	client_message.format = 32;
	client_message.window = vc->xcb.window;
	client_message.type = XCB_ATOM_NOTICE;

	xcb_send_event(vc->xcb.conn, 0, vc->xcb.window, 0, (char *) &client_message);
	xcb_flush(vc->xcb.conn);
}

static void
mainloop_xcb(struct vkcube *vc)
{
	xcb_generic_event_t *event;
	xcb_key_press_event_t *key_press;
	xcb_client_message_event_t *client_message;
	xcb_configure_notify_event_t *configure;

	while (1) 
	{
		// printf("looptydoop\n");
		bool repaint = true;
		event = xcb_wait_for_event(vc->xcb.conn);
		while (event) 
		{
			switch (event->response_type & 0x7f) 
			{
			case XCB_CLIENT_MESSAGE:
				client_message = (xcb_client_message_event_t *) event;
				if (client_message->window != vc->xcb.window)
				{
					break;
				}


				if (client_message->type == vc->xcb.atom_wm_protocols &&
					client_message->data.data32[0] == vc->xcb.atom_wm_delete_window) 
				{
					exit(0);
				}

				if (client_message->type == XCB_ATOM_NOTICE)
				{
					repaint = true;
				}
				break;

			case XCB_CONFIGURE_NOTIFY:
				configure = (xcb_configure_notify_event_t *) event;
				if (vc->width != configure->width ||
					vc->height != configure->height) 
				{
					if (vc->image_count > 0) 
					{
						vkDestroySwapchainKHR(vc->device, vc->swap_chain, NULL);
						vc->image_count = 0;
					}

					vc->width = configure->width;
					vc->height = configure->height;
				}
				break;

			case XCB_EXPOSE:
				schedule_xcb_repaint(vc);
				break;

			case XCB_KEY_PRESS:
				key_press = (xcb_key_press_event_t *) event;

				if (key_press->detail == 9)
				{
					exit(0);
				}

				break;
			}
			free(event);

			event = xcb_poll_for_event(vc->xcb.conn);
		}

		if (repaint) 
		{
			if (vc->image_count == 0)
			{
				create_swapchain(vc);
			}

			uint32_t index;
			VkResult result;
			result = vkAcquireNextImageKHR(vc->device, vc->swap_chain, 60, vc->semaphore, VK_NULL_HANDLE, &index);

			switch (result)
			{
			case VK_SUCCESS:
				break;
			case VK_NOT_READY: /* try later */
			case VK_TIMEOUT:   /* try later */
			case VK_ERROR_OUT_OF_DATE_KHR: /* handled by native events */
				schedule_xcb_repaint(vc);
				continue;
			default:
				return;
			}

			// // assert(index <= MAX_NUM_IMAGES);
			// printf("rendering\n");
			// vc->model.render(vc, &vc->buffers[index], true);
			render_cube(vc, &vc->buffers[index], true);

			vkQueuePresentKHR(
				vc->queue,
				&(VkPresentInfoKHR) 
				{
					.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
					.swapchainCount = 1,
					.pSwapchains = (VkSwapchainKHR[]) { vc->swap_chain, },
					.pImageIndices = (uint32_t[]) { index, },
					.pResults = &result,
				}
			);

			// printf("finished rendering\n");

			vkQueueWaitIdle(vc->queue);

			schedule_xcb_repaint(vc);
		}

		xcb_flush(vc->xcb.conn);

		// printf("end of story\n");
	}
}

static int display_idx = -1;
static int display_mode_idx = -1;
static int display_plane_idx = -1;

extern struct model cube_model;

int main(int argc, char *argv[])
{
	struct vkcube vc;

	// vc.model = cube_model;
	vc.gbm_device = NULL;
	vc.xcb.window = XCB_NONE;
	vc.width = width;
	vc.height = height;
	vc.protected_en = protected_chain;
	gettimeofday(&vc.start_tv, NULL);

	if (init_xcb(&vc) == -1)
	{
		printf("failed to initialize xcb\n");
	}
	else 
	{
		printf("successfully initialized xcb\n");
	}
	printf("ok");
	mainloop_xcb(&vc);

	return 0;
}