#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// With `GLFW_INCLUDE_VULKAN` defined, GLFW automatically loads the Vulkan
// header.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "arena.h"
#include "debug.h"


#define MAX(a, b)	(a > b ? a : b)
#define MIN(a, b)	(a < b ? a : b)
#define CLAMP(n, low, high)	MIN(high, MAX(low, n))


#define WINDOW_WIDTH	800
#define WINDOW_HEIGHT	600
#define WINDOW_TITLE	"Vulkan Triangle"

#define ENGINE_NAME	"No Engine"

// A value greater than 1 allows frames to be processed concurrently.
#define MAX_FRAMES_IN_FLIGHT	2


typedef struct {
	bool graphics_family_exists;
	uint32_t graphics_family;

	bool presentation_family_exists;
	uint32_t presentation_family;
} Queue_Family_Indices;

typedef struct {
	VkPhysicalDevice device;
	Queue_Family_Indices indices;
} Physical_Device;

typedef struct {
	VkSurfaceCapabilitiesKHR capabilities;

	uint32_t n_formats;
	VkSurfaceFormatKHR *formats;

	uint32_t n_presentation_modes;
	VkPresentModeKHR *presentation_modes;
} Swapchain_Support_Details;

typedef struct {
	size_t length;
	char *contents;
} Buffer;


#ifdef DEBUG
#	define ENABLE_VALIDATION_LAYERS

	static const char *validation_layers[] = {
		"VK_LAYER_KHRONOS_validation",
	};
#	define VALIDATION_LAYERS_LENGTH (sizeof(validation_layers) / sizeof(const char *))
#endif


#define ARENA_BUFFER_LENGTH	65536
static unsigned char global_arena_buffer[ARENA_BUFFER_LENGTH];
static Arena global_arena;


static const char *device_extensions[] = {
	// Present rendered images from a device to a window on the screen.
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};
#define DEVICE_EXTENSIONS_LENGTH	(sizeof(device_extensions) / sizeof(const char *))


Buffer
read_file(char *filename)
{
	FILE *file = fopen(filename, "rb");
	if (!file) {
		fprintf(stderr, "[ERROR] failed to open file %s\n", filename);
		exit(EXIT_FAILURE);
	}

	if (fseek(file, 0L, SEEK_END) == -1) {
		fprintf(stderr, "[ERROR] failed to seek to end of file %s\n", filename);
		exit(EXIT_FAILURE);
	}
	long length = ftell(file);
	if (length == -1) {
		fprintf(stderr, "[ERROR] failed to return position in file %s\n", filename);
		exit(EXIT_FAILURE);
	}
	rewind(file);

	char *contents = arena_alloc(&global_arena, length * sizeof(char));
	if (fread(contents, sizeof(char), length, file) != (unsigned)length) {
		fprintf(stderr, "[ERROR] failed to read contents of file %s\n", filename);
		exit(EXIT_FAILURE);
	}

	fclose(file);
	return (Buffer){
		.length = length,
		.contents = contents,
	};
}

Queue_Family_Indices
find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
{
	Queue_Family_Indices indices = {0};
	Arena_Checkpoint checkpoint = arena_create_checkpoint(&global_arena);

	uint32_t n_queue_families = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &n_queue_families, NULL);

	VkQueueFamilyProperties *queue_families = arena_alloc(&global_arena,
		n_queue_families * sizeof(VkQueueFamilyProperties));
	assert(queue_families);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &n_queue_families, queue_families);

	for (size_t i = 0; i < n_queue_families; ++i) {
		if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphics_family_exists = true;
			indices.graphics_family = i;
		}

		VkBool32 presentation_support = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentation_support);
		if (presentation_support) {
			indices.presentation_family_exists = true;
			indices.presentation_family = i;
		}

		if (indices.graphics_family_exists && indices.presentation_family_exists) break;
	}

	arena_restore(checkpoint);
	return indices;
}

VkShaderModule
create_shader_module(VkDevice device, Buffer shader_source)
{
	VkShaderModuleCreateInfo shader_module_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = shader_source.length,
		.pCode = (uint32_t *)shader_source.contents,
	};

	VkShaderModule shader_module = {0};
	if (vkCreateShaderModule(device, &shader_module_info, NULL, &shader_module) != VK_SUCCESS) {
		fprintf(stderr, "[ERROR] failed to create shader module\n");
		exit(EXIT_FAILURE);
	}

	return shader_module;
}

int
main(void)
{
	/* ---
	 * Initialize global linear allocator to simplify memory management.
	 * ---
	 */
	arena_init(&global_arena, global_arena_buffer, ARENA_BUFFER_LENGTH);


	/* ---
	 * Open a window using GLFW
	 * ---
	 */
	GLFWwindow *window = NULL;
	{
		glfwInit();

		// Do not create an OpenGL context with GLFW.
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		// Disable window resize because it complicates this simple example.
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE, NULL, NULL);
		if (window == NULL) {
			fprintf(stderr, "[ERROR] failed to create window with GLFW\n");
			exit(EXIT_FAILURE);
		}
	}


	/* ---
	 * Initialze an instance of Vulkan.
	 * ---
	 */
	VkInstance instance = {0};
	{
		// Query GLFW for the Vulkan extensions it requires.
		uint32_t n_extensions = 0;
		const char **extensions = glfwGetRequiredInstanceExtensions(&n_extensions);

		// NOTE This application info is not strictly required by Vulkan, but it
		// may provide the driver with some information that enables additional
		// optimizations. On the other hand, Vulkan requires the instance info
		// defined afterward -- it specifies which global extensions and
		// validation layers to use.
		VkApplicationInfo app_info = {
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = WINDOW_TITLE,
			.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
			.pEngineName = ENGINE_NAME,
			.engineVersion = VK_MAKE_VERSION(1, 0, 0),
			.apiVersion = VK_API_VERSION_1_0,
		};
		VkInstanceCreateInfo instance_info = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &app_info,
			.enabledExtensionCount = n_extensions,
			.ppEnabledExtensionNames = extensions,
#ifdef ENABLE_VALIDATION_LAYERS
			.enabledLayerCount = VALIDATION_LAYERS_LENGTH,
			.ppEnabledLayerNames = validation_layers,
#endif
		};

		// NOTE The program terminates here if the system doesn't support a
		// specified extension.
		if (vkCreateInstance(&instance_info, NULL, &instance) != VK_SUCCESS) {
			fprintf(stderr, "[ERROR] failed to create Vulkan instance\n");
			exit(EXIT_FAILURE);
		}
	}


	/* ---
	 * Initialize a surface with GLFW.
	 * ---
	 */
	VkSurfaceKHR surface = {0};
	if (glfwCreateWindowSurface(instance, window, NULL, &surface) != VK_SUCCESS) {
		fprintf(stderr, "[ERROR] failed to create window surface\n");
		exit(EXIT_FAILURE);
	}


	/* ---
	 * Select a physical GPU to use.
	 * ---
	 */
	Physical_Device physical_device = { .device = VK_NULL_HANDLE };
	{
		// Query the number of physical devices available.
		uint32_t n_devices = 0;
		vkEnumeratePhysicalDevices(instance, &n_devices, NULL);

		if (n_devices == 0) {
			fprintf(stderr, "[ERROR] failed to find any GPU that supports Vulkan\n");
			exit(EXIT_FAILURE);
		}

		Arena_Checkpoint checkpoint = arena_create_checkpoint(&global_arena);

		// Retrieve a list GPUs in the system that support Vulkan.
		VkPhysicalDevice *devices = arena_alloc(&global_arena, n_devices * sizeof(VkPhysicalDevice));
		assert(devices);
		vkEnumeratePhysicalDevices(instance, &n_devices, devices);

		// Scan all devices to find a suitable device, where a suitable device is
		// a GPU with support for Vulkan's graphics and present queue families.
		// That's basically any GPU with Vulkan support.
		for (size_t i = 0; i < n_devices; ++i) {
			VkPhysicalDevice device = devices[i];

			// TODO Define find_queue_families() inline here since it's not used
			// elsewhere. Rename `presentation_family` too.
			Queue_Family_Indices indices = find_queue_families(device, surface);
			if (indices.graphics_family_exists && indices.presentation_family_exists) {
				physical_device.device = device;
				physical_device.indices = indices;
				break;
			}
		}

		if (physical_device.device == VK_NULL_HANDLE) {
			fprintf(stderr, "[ERROR] failed to find a suitable GPU\n");
			exit(EXIT_FAILURE);
		}

		// The other physical devices are no longer necessary to store in memory.
		arena_restore(checkpoint);
	}


	/* ---
	 * Create a logical device and its corresponding queues to interface with
	 * the selected physical device.
	 * ---
	 */
	VkDevice device = {0};
	VkQueue graphics_queue = {0};
	VkQueue present_queue = {0};
	{
		// FIXME These queue families are not required to be the same, though they
		// often are. For now, this assumption simplifies the code. That being
		// said, a physical device that supports graphics and presentation in the
		// same queue is faster.
		assert(physical_device.indices.graphics_family == physical_device.indices.presentation_family);

		float queue_priority = 1.0f;
		VkDeviceQueueCreateInfo queue_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = physical_device.indices.graphics_family,
			.queueCount = 1,
			.pQueuePriorities = &queue_priority,
		};

		VkPhysicalDeviceFeatures features = {0};
		VkDeviceCreateInfo device_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queue_info,
			.pEnabledFeatures = &features,
			.enabledExtensionCount = DEVICE_EXTENSIONS_LENGTH,
			.ppEnabledExtensionNames = device_extensions,
#ifdef ENABLE_VALIDATION_LAYERS
			// Previous versions of Vulkan expected some validation layers to be
			// specified per device. Define them here as well for compatibility.
			.enabledLayerCount = VALIDATION_LAYERS_LENGTH,
			.ppEnabledLayerNames = validation_layers,
#endif
		};

		if (vkCreateDevice(physical_device.device, &device_info, NULL, &device) != VK_SUCCESS) {
			fprintf(stderr, "[ERROR] failed to create logical device\n");
			exit(EXIT_FAILURE);
		}

		// Vulkan automatically creates a queue along with a logical device. Get a
		// handle for each queue.
		vkGetDeviceQueue(device, physical_device.indices.graphics_family, 0, &graphics_queue);
		vkGetDeviceQueue(device, physical_device.indices.presentation_family, 0, &present_queue);
		assert(graphics_queue == present_queue);
	}


	/* ---
	 * Create swapchain -- a queue of images to present.
	 *
	 * TODO Combine Swapchain_Support_Details and the three independent
	 * variables defined below. In other words, use the struct instead of the
	 * indepdents. Actually scrap that, I think all of these values are stored
	 * in the swapchain struct anyway.
	 *
	 * Choose the most optimal settings for the swapchain. The swap extent
	 * determines the resolution of images in the swapchain; the surface format
	 * determines their color depth; and the presentation mode determines the
	 * conditions for swapping images onto the screen. 
	 * ---
	 */
	VkSwapchainKHR swapchain = {0};
	VkImage *images = NULL;
	uint32_t n_images = 0;
	VkSurfaceFormatKHR surface_format = { .format = VK_FORMAT_UNDEFINED };
	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	VkExtent2D extent = {0};
	{
		Swapchain_Support_Details details = {0};

		// Query selected physical device for its supported surface capabilities.
		// The remainder of this block ensures the device supports the ones
		// required.
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device.device, surface, &details.capabilities);

		/* Determine proper resolution for swap chain images. */
		{
			VkSurfaceCapabilitiesKHR capabilities = details.capabilities;

			if (capabilities.currentExtent.width != UINT32_MAX) {
				extent = capabilities.currentExtent;
			} else {
				int width = 0;
				int height = 0;

				glfwGetFramebufferSize(window, &width, &height);

				extent.width = CLAMP((unsigned)width, capabilities.minImageExtent.width,
					capabilities.maxImageExtent.width);
				extent.height = CLAMP((unsigned)height, capabilities.minImageExtent.height,
					capabilities.maxImageExtent.height);
			}
		}

		/* Confirm supported surface formats. */
		{
			Arena_Checkpoint checkpoint = arena_create_checkpoint(&global_arena);

			// Get the number of supported surface formats.
			vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device.device, surface, &details.n_formats, NULL);
			if (details.n_formats == 0) {
				fprintf(stderr, "[ERROR] the selected physical device does not support any common surface formats\n");
				exit(EXIT_FAILURE);
			}

			// Allocate an array and store the supported surface formats in it.
			details.formats = arena_alloc(&global_arena, details.n_formats * sizeof(VkSurfaceFormatKHR));
			assert(details.formats);
			vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device.device,
				surface, &details.n_formats, details.formats);

			// Ideally, choose an SRGB color space because for more accurate color
			// production. Otherwise, default to whichever color space is available.
			for (size_t i = 0; i < details.n_formats; ++i) {
				VkSurfaceFormatKHR this_format = details.formats[i];
				if (this_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
						this_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
					surface_format = this_format;
					break;
				}
			}
			if (surface_format.format == VK_FORMAT_UNDEFINED) surface_format = details.formats[0];

			arena_restore(checkpoint);
		}

		/* Confirm supported present modes. */
		{
			Arena_Checkpoint checkpoint = arena_create_checkpoint(&global_arena);

			// Get the number of supported present modes.
			vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device.device, surface,
				&details.n_presentation_modes, NULL);

			// Allocate an array and store the supported present modes in it.
			details.presentation_modes = arena_alloc(&global_arena,
				details.n_presentation_modes * sizeof(VkPresentModeKHR));
			assert(details.presentation_modes);
			vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device.device, surface,
				&details.n_presentation_modes, details.presentation_modes);

			// Ideally, choose VK_PRESENT_MAILBOX_KHR as the presentation mode for
			// the swapchain. However, VK_PRESENT_MODE_FIFO_KHR is the only
			// guaranteed mode, so it's set as the default. VK_PRESENT_MODE_FIFO_KHR
			// blocks insertions to the swapchain when it's full; this may cause
			// tearing. To avoid tearing, VK_PRESENT_MAILBOX_KHR replaces queued
			// images with newer ones when the swapchain is full. Note,
			// VK_PRESENT_MODE_MAILBOX_KHR also demands a more performant GPU.
			for (size_t i = 0; i < details.n_presentation_modes; ++i) {
				VkPresentModeKHR this_present_mode = details.presentation_modes[i];
				if (this_present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
					present_mode = this_present_mode;
					break;
				}
			}

			arena_restore(checkpoint);
		}

		// Specify the number of images in the swapchain. This value is bounded
		// below by the capabilities of the physical device, but it's better for
		// performance to allow at least one additional image in the swapchain.
		n_images = details.capabilities.minImageCount + 1;
		if (details.capabilities.maxImageCount > 0 && n_images > details.capabilities.maxImageCount) {
			// NOTE A maximum image count of 0 in this case means the number of
			// images in the swapchain is not explicitly bounded above by the
			// device.
			n_images = details.capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR swapchain_info = {
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = surface,
			.minImageCount = n_images,
			.imageFormat = surface_format.format,
			.imageColorSpace = surface_format.colorSpace,
			.imageExtent = extent,
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.preTransform = details.capabilities.currentTransform,
			.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode = present_mode,
			.clipped = VK_TRUE,

			// A window resize invalidates the current swapchain. In that case, a
			// new one must be initialized and this stores a handle to the previous
			// swapchain.
			.oldSwapchain = VK_NULL_HANDLE,

			// NOTE This assumes indices for graphics family and presentation
			// family are the same.
			.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		if (vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain) != VK_SUCCESS) {
			fprintf(stderr, "[ERROR] failed to create swapchain\n");
			exit(EXIT_FAILURE);
		}

		vkGetSwapchainImagesKHR(device, swapchain, &n_images, NULL);
		images = arena_alloc(&global_arena, n_images * sizeof(VkImage));
		assert(images);
		vkGetSwapchainImagesKHR(device, swapchain, &n_images, images);
	}


	/* ---
	 * Create image views.
	 *
	 * Image views are handles to the images in the swapchain -- they're used
	 * during render operations.
	 * ---
	 */
	VkImageView *image_views = NULL;
	{
		image_views = arena_alloc(&global_arena, n_images * sizeof(VkImageView));
		assert(image_views);

		VkImageViewCreateInfo image_view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = surface_format.format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};
		for (size_t i = 0; i < n_images; ++i) {
			image_view_info.image = images[i];
			if (vkCreateImageView(device, &image_view_info, NULL, &image_views[i]) != VK_SUCCESS) {
				fprintf(stderr, "[ERROR] failed to create image view\n");
				exit(EXIT_FAILURE);
			}
		}
	}


	/* ---
	 * Create render pass.
	 * ---
	 */
	VkRenderPass render_pass = {0};
	{
		VkAttachmentDescription color_attachment = {
			.format = surface_format.format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		};
		VkAttachmentReference color_attachment_reference = {
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};
		VkSubpassDescription subpass = {
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.colorAttachmentCount = 1,
			.pColorAttachments = &color_attachment_reference,
		};

		// Subpasses in a render pass handle image layout transitions.
		VkSubpassDependency dependency = {
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		};
		VkRenderPassCreateInfo render_pass_info = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &color_attachment,
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = 1,
			.pDependencies = &dependency,
		};

		if (vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass) != VK_SUCCESS) {
			fprintf(stderr, "[ERROR] failed to create render pass\n");
			exit(EXIT_FAILURE);
		}
	}


	/* ---
	 * Create graphics pipeline and shader stage.
	 * ---
	 */
	VkPipelineLayout layout = {0};
	VkPipeline graphics_pipeline = {0};
	VkShaderModule vert_shader_module = {0};
	VkShaderModule frag_shader_module = {0};
	{
		Buffer vert_shader_source = read_file("shaders/vert.spv");
		Buffer frag_shader_source = read_file("shaders/frag.spv");

		vert_shader_module = create_shader_module(device, vert_shader_source);
		frag_shader_module = create_shader_module(device, frag_shader_source);

		// The vertex shader processes each vertex.
		VkPipelineShaderStageCreateInfo vert_shader_stage_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vert_shader_module,
			.pName = "main",
		};
		// The fragment shader provides depth and color to the images.
		VkPipelineShaderStageCreateInfo frag_shader_stage_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = frag_shader_module,
			.pName = "main",
		};
		VkPipelineShaderStageCreateInfo shader_stages[] = {
			vert_shader_stage_info,
			frag_shader_stage_info,
		};

		// Indicate no vertex data exists to be input to the vertex shader since
		// the vertex data is hard coded in the shader directly.
		VkPipelineVertexInputStateCreateInfo vertex_input_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 0,
			.pVertexBindingDescriptions = NULL,
			.vertexAttributeDescriptionCount = 0,
			.pVertexAttributeDescriptions = NULL,
		};

		// Indicate a triangle should be formed out of the vertices.
		VkPipelineInputAssemblyStateCreateInfo input_assembly = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.primitiveRestartEnable = VK_FALSE,
		};

		// Specify viewport and scissor filter dynamically as opposed to statically
		// in the pipeline. 
		VkDynamicState dynamic_states[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};
		VkPipelineDynamicStateCreateInfo dynamic_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 2,
			.pDynamicStates = dynamic_states,
		};

		// A viewport describes the region of the framebuffer to which to output
		// the render.
		VkViewport viewport = {
			.x = 0.0f,
			.y = 0.0f,
			.width = extent.width,
			.height = extent.height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};

		// Pixels outside the bounds specified by the scissor rectangle are
		// discarded by the rasterizer.
		VkRect2D scissor = {
			.offset = {0, 0},
			.extent = extent,
		};
		VkPipelineViewportStateCreateInfo viewport_state_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.pViewports = &viewport,
			.scissorCount = 1,
			.pScissors = &scissor,
		};

		// Given vertices from the vertex shader, the rasterizer yields fragments
		// for the fragment shader to transform.
		VkPipelineRasterizationStateCreateInfo rasterizer = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.lineWidth = 1.0f,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
		};

		// Disable multisampling, a form of antialiasing.
		VkPipelineMultisampleStateCreateInfo multisampling = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.sampleShadingEnable = VK_FALSE,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		};

		VkPipelineColorBlendAttachmentState color_blend_attachment = {
			.colorWriteMask =
				VK_COLOR_COMPONENT_R_BIT |
				VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT |
				VK_COLOR_COMPONENT_A_BIT,
			.blendEnable = VK_TRUE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.alphaBlendOp = VK_BLEND_OP_ADD,
		};
		VkPipelineColorBlendStateCreateInfo color_blending = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount = 1,
			.pAttachments = &color_blend_attachment,
		};

		VkPipelineLayoutCreateInfo layout_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		};
		if (vkCreatePipelineLayout(device, &layout_info, NULL, &layout) != VK_SUCCESS) {
			fprintf(stderr, "[ERROR] failed to create pipeline layout\n");
			exit(EXIT_FAILURE);
		}

		VkGraphicsPipelineCreateInfo pipeline_info = {
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = 2,
			.pStages = shader_stages,
			.pVertexInputState = &vertex_input_info,
			.pInputAssemblyState = &input_assembly,
			.pViewportState = &viewport_state_info,
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling,
			.pColorBlendState = &color_blending,
			.pDynamicState = &dynamic_state,
			.layout = layout,
			.renderPass = render_pass,
			.subpass = 0,
		};

		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL,
					&graphics_pipeline) != VK_SUCCESS) {
			fprintf(stderr, "[ERROR] failed to create graphics pipeline\n");
			exit(EXIT_FAILURE);
		}

		vkDestroyShaderModule(device, frag_shader_module, NULL);
		vkDestroyShaderModule(device, vert_shader_module, NULL);
	}


	/* ---
	 * Create framebuffers.
	 * ---
	 */
	VkFramebuffer *framebuffers = NULL;
	{
		framebuffers = arena_alloc(&global_arena, n_images * sizeof(VkFramebuffer));
		assert(framebuffers);
		for (size_t i = 0; i < n_images; ++i) {
			VkImageView attachments[] = { image_views[i] };

			VkFramebufferCreateInfo framebuffer_info = {
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.renderPass = render_pass,
				.attachmentCount = 1,
				.pAttachments = attachments,
				.width = extent.width,
				.height = extent.height,
				.layers = 1,
			};

			if (vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffers[i]) != VK_SUCCESS) {
				fprintf(stderr, "[ERROR] failed to create framebuffer\n");
				exit(EXIT_FAILURE);
			}
		}
	}


	/* ---
	 * Initialize command pool.
	 *
	 * A command pool manages the memory that its command buffers allocate.
	 * ---
	 */
	VkCommandPool command_pool = {0};
	{
		VkCommandPoolCreateInfo pool_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = physical_device.indices.graphics_family,
		};

		if (vkCreateCommandPool(device, &pool_info, NULL, &command_pool) != VK_SUCCESS) {
			fprintf(stderr, "[ERROR] failed to create command pool\n");
			exit(EXIT_FAILURE);
		}
	}


	/* ---
	 * Allocate a command buffer for every frame in flight.
	 *
	 * A command buffer records commands such as drawing operations and memory
	 * transfers and then submits this series of commands together for
	 * processing.
	 * ---
	 */
	VkCommandBuffer command_buffer[MAX_FRAMES_IN_FLIGHT] = {0};
	{
		VkCommandBufferAllocateInfo command_buffer_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 2,
		};

		if (vkAllocateCommandBuffers(device, &command_buffer_info, command_buffer) != VK_SUCCESS) {
			fprintf(stderr, "[ERROR] failed to allocate command buffer\n");
			exit(EXIT_FAILURE);
		}
	};

	
	/* ---
	 * Initialize semaphores and fences.
	 * ---
	 */
	VkSemaphore image_available_semaphore[MAX_FRAMES_IN_FLIGHT] = {0};
	VkSemaphore render_finished_semaphore[MAX_FRAMES_IN_FLIGHT] = {0};
	VkFence in_flight_fence[MAX_FRAMES_IN_FLIGHT] = {0};
	{
		VkSemaphoreCreateInfo semaphore_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};
		// Initialize the fence in a signaled state so the CPU doesn't block on the
		// first frame.
		VkFenceCreateInfo fence_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			if (vkCreateSemaphore(device, &semaphore_info, NULL, &image_available_semaphore[i]) != VK_SUCCESS) {
				fprintf(stderr, "[ERROR] failed to create semaphore\n");
				exit(EXIT_FAILURE);
			}
			if (vkCreateSemaphore(device, &semaphore_info, NULL, &render_finished_semaphore[i]) != VK_SUCCESS) {
				fprintf(stderr, "[ERROR] failed to create semaphore\n");
				exit(EXIT_FAILURE);
			}
			if (vkCreateFence(device, &fence_info, NULL, &in_flight_fence[i]) != VK_SUCCESS) {
				fprintf(stderr, "[ERROR] failed to create fence\n");
				exit(EXIT_FAILURE);
			}
		}
	}


	/* ---
	 * Loop. Start event loop and rendering to screen.
	 * ---
	 */
	uint32_t current_frame = 0;
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		{
			// Wait an unbounded amonut of time for the previous frame to finish.
			vkWaitForFences(device, 1, &in_flight_fence[current_frame], VK_TRUE, UINT64_MAX);
			vkResetFences(device, 1, &in_flight_fence[current_frame]);

			// Get an index to an image from the swapchain.
			uint32_t image_index = 0;
			vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available_semaphore[current_frame],
				VK_NULL_HANDLE, &image_index);


			/* Add draw commands into the buffer for the current frame. */
			{
				vkResetCommandBuffer(command_buffer[current_frame], 0);

				VkCommandBufferBeginInfo begin_info = {
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
					.flags = 0,
					.pInheritanceInfo = NULL,
				};
				if (vkBeginCommandBuffer(command_buffer[current_frame], &begin_info) != VK_SUCCESS) {
					fprintf(stderr, "[ERROR] failed to begin recording command buffer\n");
					exit(EXIT_FAILURE);
				}

				VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
				VkRenderPassBeginInfo render_pass_info = {
					.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
					.renderPass = render_pass,
					.framebuffer = framebuffers[image_index],
					.renderArea = {
						.offset = {0, 0},
						.extent = extent,
					},
					.clearValueCount = 1,
					.pClearValues = &clear_color,
				};

				vkCmdBeginRenderPass(command_buffer[current_frame], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
				{
					vkCmdBindPipeline(command_buffer[current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

					// Define the viewport and the scissor rectangle dynamically as
					// specified when initializing the pipeline.
					VkViewport viewport = {
						.x = 0.0f,
						.y = 0.0f,
						.width = extent.width,
						.height = extent.height,
						.minDepth = 0.0f,
						.maxDepth = 1.0f,
					};
					VkRect2D scissor = {
						.offset = {0, 0},
						.extent = extent,
					};
					vkCmdSetViewport(command_buffer[current_frame], 0, 1, &viewport);
					vkCmdSetScissor(command_buffer[current_frame], 0, 1, &scissor);

					// Draw the triangle.
					vkCmdDraw(command_buffer[current_frame], 3, 1, 0, 0);
				}
				vkCmdEndRenderPass(command_buffer[current_frame]);

				if (vkEndCommandBuffer(command_buffer[current_frame]) != VK_SUCCESS) {
					fprintf(stderr, "[ERROR] failed to record command buffer\n");
					exit(EXIT_FAILURE);
				}
			}

			VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
			VkSemaphore wait_semaphores[] = {image_available_semaphore[current_frame]};
			VkSemaphore signal_semaphores[] = {render_finished_semaphore[current_frame]};

			VkSubmitInfo submit_info = {
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = wait_semaphores,
				.pWaitDstStageMask = wait_stages,
				.signalSemaphoreCount = 1,
				.pSignalSemaphores = signal_semaphores,
				.commandBufferCount = 1,
				.pCommandBuffers = &command_buffer[current_frame],
			};

			// Submit the newly recorded command buffer.
			if (vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fence[current_frame]) != VK_SUCCESS) {
				fprintf(stderr, "[ERROR] failed to submit draw command buffer\n");
				exit(EXIT_FAILURE);
			}

			// Display the rendered image.
			VkSwapchainKHR swapchains[] = {swapchain};
			VkPresentInfoKHR present_info = {
				.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = signal_semaphores,
				.swapchainCount = 1,
				.pSwapchains = swapchains,
				.pImageIndices = &image_index,
				.pResults = NULL,
			};
			vkQueuePresentKHR(present_queue, &present_info);
		}

		current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	// Wait for the logical device to finish executing any commands.
	vkDeviceWaitIdle(device);


	/* ---
	 * Clean resources and exit.
	 * ---
	 */
	{
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			vkDestroyFence(device, in_flight_fence[i], NULL);
			vkDestroySemaphore(device, render_finished_semaphore[i], NULL);
			vkDestroySemaphore(device, image_available_semaphore[i], NULL);
		}
		vkDestroyCommandPool(device, command_pool, NULL);
		vkDestroyPipeline(device, graphics_pipeline, NULL);
		vkDestroyPipelineLayout(device, layout, NULL);
		vkDestroyRenderPass(device, render_pass, NULL);
		for (size_t i = 0; i < n_images; ++i) {
			vkDestroyFramebuffer(device, framebuffers[i], NULL);
			vkDestroyImageView(device, image_views[i], NULL);
		}
		vkDestroySwapchainKHR(device, swapchain, NULL);
		vkDestroyDevice(device, NULL);
		vkDestroySurfaceKHR(instance, surface, NULL);
		vkDestroyInstance(instance, NULL);
		glfwDestroyWindow(window);
		glfwTerminate();
	}

	exit(EXIT_SUCCESS);
}
