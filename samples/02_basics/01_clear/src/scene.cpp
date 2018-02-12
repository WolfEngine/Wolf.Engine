#include "pch.h"
#include "scene.h"

using namespace std;
using namespace wolf;
using namespace wolf::system;
using namespace wolf::graphics;

scene::scene(_In_z_ const std::wstring& pContentPath, _In_z_ const std::wstring& pLogPath, _In_z_ const std::wstring& pAppName) :
	w_game(pContentPath, pLogPath, pAppName)
{
	w_graphics_device_manager_configs _config;
	_config.debug_gpu = true;
	w_game::set_graphics_device_manager_configs(_config);

	w_game::set_fixed_time_step(false);
}


scene::~scene()
{
	//release all resources
	release();
}

void scene::initialize(_In_ std::map<int, w_window_info> pOutputWindowsInfo)
{
	// TODO: Add your pre-initialization logic here

	w_game::initialize(pOutputWindowsInfo);
}

void scene::load()
{
    defer(nullptr, [&](...)
    {
       w_game::load();
    });
    
    const std::string _trace_info = this->name + "::load";
    
    auto _gDevice = this->graphics_devices[0];
    auto _output_window = &(_gDevice->output_presentation_window);
    
    w_point_t _screen_size;
    _screen_size.x =  _output_window->width;
    _screen_size.y = _output_window->height;
    
    //initialize viewport
    this->_viewport.y = 0;
    this->_viewport.width = static_cast<float>(_screen_size.x);
    this->_viewport.height = static_cast<float>(_screen_size.y);
    this->_viewport.minDepth = 0;
    this->_viewport.maxDepth = 1;
    
    //initialize scissor of viewport
    this->_viewport_scissor.offset.x = 0;
    this->_viewport_scissor.offset.y = 0;
    this->_viewport_scissor.extent.width = _screen_size.x;
    this->_viewport_scissor.extent.height = _screen_size.y;
    
	//define color and depth as an attachments buffers for render pass
	std::vector<std::vector<w_image_view>> _render_pass_attachments;
	for (size_t i = 0; i < _output_window->vk_swap_chain_image_views.size(); ++i)
	{
		_render_pass_attachments.push_back
		(
			//COLOR										  , DEPTH
			{ _output_window->vk_swap_chain_image_views[i], _output_window->vk_depth_buffer_image_view }
		);
	}
    
    //create render pass
	auto _hr = this->_draw_render_pass.load(
		_gDevice,
		_viewport,
		_viewport_scissor,
		_render_pass_attachments);
	if (_hr == W_FAILED)
	{
		release();
		V(W_FAILED, "creating render pass", _trace_info, 3, true);
	}

    //create semaphore create info
    _hr = this->_draw_semaphore.initialize(_gDevice);
    if (_hr == W_FAILED)
    {
        release();
        V(W_FAILED, "creating semaphore for draw command buffer", _trace_info, 3, true);
    }
    
    _hr = this->_draw_fence.initialize(_gDevice);
    if (_hr == W_FAILED)
    {
        release();
        V(W_FAILED, "creating fence for draw command buffer", _trace_info, 3, true);
    }
    
    //create two primary command buffers for clearing screen
    auto _swap_chain_image_size = _output_window->vk_swap_chain_image_views.size();
    _hr = this->_draw_command_buffers.load(_gDevice, _swap_chain_image_size);
    if (_hr == W_FAILED)
    {
        release();
        V(W_FAILED, "creating draw command buffers", _trace_info, 3, true);
    }
    
    _build_draw_command_buffers();
}

HRESULT scene::_build_draw_command_buffers()
{
    auto _size = this->_draw_command_buffers.get_commands_size();
	auto _cmd = &this->_draw_command_buffers;
    for (uint32_t i = 0; i < _size; ++i)
    {
        this->_draw_command_buffers.begin(i);
        {
			this->_draw_render_pass.begin(
				i,
				_cmd,
				w_color::CORNFLOWER_BLUE(),
				1.0f,
				0.0f);
            {
                //place your draw code
            }
            this->_draw_render_pass.end(_cmd);
        }
        this->_draw_command_buffers.end(i);
    }
	_cmd = nullptr;
    return W_PASSED;
}

void scene::update(_In_ const wolf::system::w_game_time& pGameTime)
{
    if (w_game::exiting) return;
    const std::string _trace_info = this->name + "::update";
    
    defer(nullptr, [&](...)
    {
        w_game::update(pGameTime);
    });

	OutputDebugString((std::to_wstring(pGameTime.get_frames_per_second()) + L"\r\n").c_str());
	//wolf::logger.write(std::to_string(pGameTime.get_frames_per_second()));
}

W_RESULT scene::render(_In_ const wolf::system::w_game_time& pGameTime)
{
	if (w_game::exiting) return W_PASSED;

	const std::string _trace_info = this->name + "::render";

	auto _gDevice = this->graphics_devices[0];
	auto _output_window = &(_gDevice->output_presentation_window);
	auto _frame_index = _output_window->swap_chain_image_index;

	const w_pipeline_stage_flags _wait_dst_stage_mask[] =
	{
		w_pipeline_stage_flag_bits::W_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	};

	//set active command buffer
	this->_draw_command_buffers.set_active_command(_frame_index);
	//reset draw fence
	this->_draw_fence.reset();
	if (_gDevice->submit(
		{ &this->_draw_command_buffers }, //command buffers
		_gDevice->vk_graphics_queue, //graphics queue
		&_wait_dst_stage_mask[0], //destination masks
		{ _output_window->swap_chain_image_is_available_semaphore }, //wait semaphores
		{ _output_window->rendering_done_semaphore },//signal semaphores
		&this->_draw_fence) == W_FAILED)
	{
		V(W_FAILED, "submiting queue for drawing gui", _trace_info, 3, true);
	}
	// Wait for fence to signal that all command buffers are ready
	this->_draw_fence.wait();

	return w_game::render(pGameTime);
}

void scene::on_window_resized(_In_ const uint32_t& pGraphicsDeviceIndex, _In_ const w_point& pNewSizeOfWindow)
{
    w_game::on_window_resized(pGraphicsDeviceIndex, pNewSizeOfWindow);
}

void scene::on_device_lost()
{
    w_game::on_device_lost();
}

ULONG scene::release()
{
	if (this->get_is_released()) return 1;
    
    this->_draw_fence.release();
    this->_draw_semaphore.release();
    
    this->_draw_command_buffers.release();
    this->_draw_render_pass.release();

	return w_game::release();
}
