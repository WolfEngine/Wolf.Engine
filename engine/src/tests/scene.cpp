#include "pch.h"
#include "scene.h"
#include <w_graphics/w_pipeline.h>
#include <w_content_manager.h>
#include <w_framework/w_quad.h>
#include <imgui/imgui.h>
#include <w_graphics/w_imgui.h>

using namespace wolf::system;
using namespace wolf::graphics;
using namespace wolf::framework;
using namespace wolf::content_pipeline;

//forward declaration
static void make_gui();

#if defined(__WIN32)
scene::scene(_In_z_ const std::wstring& pRunningDirectory, _In_z_ const std::wstring& pAppName):
	w_game(pRunningDirectory, pAppName)

#elif defined(__UWP)
scene::scene(_In_z_ const std::wstring& pAppName):
	w_game(pAppName)
#else
scene::scene(_In_z_ const std::string& pRunningDirectory, _In_z_ const std::string& pAppName) :
    w_game(pRunningDirectory, pAppName)
#endif
{
    w_game::set_fixed_time_step(false);

#if defined(__WIN32) || defined(__UWP)
    auto _running_dir = pRunningDirectory;
    content_path = _running_dir + L"../../../../content/";
#elif defined(__APPLE__)
    auto _running_dir = wolf::system::convert::string_to_wstring(pRunningDirectory);
    content_path = _running_dir + L"/../../../../../content/";
#endif

    w_graphics_device_manager_configs _config;
    _config.debug_gpu = true;
    this->set_graphics_device_manager_configs(_config);
}

scene::~scene()
{
    release();
}

void scene::initialize(_In_ std::map<int, std::vector<w_window_info>> pOutputWindowsInfo)
{
    this->on_device_features_fetched += [](w_graphics_device_manager::w_device_features_extensions& pDeviceFeaturesExtensions)
    {
        auto _fe = pDeviceFeaturesExtensions.device_features;
        //if (_fe && (*_fe).tessellationShader == VK_FALSE)
        //{
        //    logger.write("Tesselation not supported for graphics device:" +
        //        std::string(pDeviceFeaturesExtensions.get_device_name()) +
        //        " ID:" + std::to_string(pDeviceFeaturesExtensions.get_device_id()));
        //}
        // Fill mode non solid is required for wireframe display
        (*_fe).fillModeNonSolid = VK_TRUE;
    };
    // TODO: Add your pre-initialization logic here
    w_game::initialize(pOutputWindowsInfo);
}

//auto _quad = new w_quad<vertex_declaration_structs::vertex_position_color_uv>();
void scene::load()
{
    auto _gDevice = this->graphics_devices[0];
    auto _output_window = &(_gDevice->output_presentation_windows[0]);
    _screen_size.x = static_cast<uint32_t>(_output_window->width);
    _screen_size.y = static_cast<uint32_t>(_output_window->height);

    w_game::load();
    
    w_viewport _viewport;
    _viewport.y = 0;
    _viewport.width = static_cast<float>(_screen_size.x);
    _viewport.height = static_cast<float>(_screen_size.y);
    _viewport.minDepth = 0;
    _viewport.maxDepth = 1;

    w_viewport_scissor _viewport_scissor;
    _viewport_scissor.offset.x = 0;
    _viewport_scissor.offset.y = 0;
    _viewport_scissor.extent.width = _screen_size.x;
    _viewport_scissor.extent.height = _screen_size.y;

    auto _depth_attachment = w_graphics_device::w_render_pass_attachments::depth_attachment_description;
    _depth_attachment.format = _output_window->vk_depth_buffer_format;

    //create render pass
    auto _hr = this->_render_pass.load(
        _gDevice,
        _viewport,
        _viewport_scissor,
        {
            w_graphics_device::w_render_pass_attachments::color_attachment_description,
            _depth_attachment,
        });

    if (_hr == S_FALSE)
    {
        logger.error("Error on creating render pass");
        release();
        exit(1);
    }
    auto _render_pass_handle = this->_render_pass.get_handle();
    
    std::string _pipeline_cache_name = "pipeline_cache";
    if (w_pipeline::create_pipeline_cache(_gDevice, _pipeline_cache_name) == S_FALSE)
    {
        logger.error("Could not create pipeline cache");
        _pipeline_cache_name.clear();
    }

    //load shader
    std::vector<w_shader_binding_param> _shader_params;

    w_shader_binding_param _param;
    _param.index = 0;
    _param.type = w_shader_binding_type::UNIFORM;
    _param.stage = w_shader_stage::VERTEX_SHADER;
    _param.uniform_info = this->_wvp_unifrom.get_descriptor_info();
    _shader_params.push_back(_param);
    
    _param.index = 1;
    _param.type = w_shader_binding_type::SAMPLER;
    _param.stage = w_shader_stage::FRAGMENT_SHADER;
    _param.sampler_info = w_texture::default_texture->get_descriptor_info();
    _shader_params.push_back(_param);

    //load shaders
    _hr = w_shader::load_to_shared_shaders(
        _gDevice,
        "indirect_shader",
        content_path + L"shaders/compute/indirect_draw.vert.spv",
        L"",
        L"",
        content_path + L"shaders/compute/indirect_draw.frag.spv",
        _shader_params,
        &this->_shader);
    if (_hr == S_FALSE)
    {
        logger.error("Error on loading indirect shader");
        w_game::exiting = true;
        return;
    }

    std::vector<VkDynamicState> _dynamic_states =
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR//,
    };

    w_vertex_binding_attributes _vertex_binding_attrs;
    _vertex_binding_attrs.declaration = w_vertex_declaration::USER_DEFINED;
    _vertex_binding_attrs.binding_attributes[0] = { Vec3, Vec2 };
    _vertex_binding_attrs.binding_attributes[1] = { Vec3, Vec3, Float };
    
    auto _descriptor_set_layout_binding = this->_shader->get_descriptor_set_layout_binding();
    this->_pipeline = new w_pipeline();
    _hr = this->_pipeline->load(
        _gDevice,
        _vertex_binding_attrs,
        VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
        "pipeline_cache",
        _render_pass_handle,
        _shader->get_shader_stages(),
        &_descriptor_set_layout_binding,
        { this->_render_pass.get_viewport() }, //viewports
        { this->_render_pass.get_viewport_scissor() }, //viewports scissor
        _dynamic_states,
        3,
        nullptr,
        nullptr,
        _output_window->vk_depth_buffer_format != VkFormat::VK_FORMAT_UNDEFINED);
    if (_hr)
    {
        logger.error("Error creating pipeline for mesh");
    }


    //load scene
    auto _scene = w_content_manager::load<w_cpipeline_scene>(content_path + L"models/suzanne_lods.dae");
    if (_scene)
    {
        //get all models
        std::vector<w_cpipeline_model*> _cmodels;
        _scene->get_all_models(_cmodels);
        
        auto _z_up = _scene->get_z_up();
        for (auto& _model : _cmodels)
        {
            //load meshes
            std::vector<w_cpipeline_model::w_mesh*> _model_meshes;
            _model->get_meshes(_model_meshes);

            for (auto& _mesh_data : _model_meshes)
            {
                auto _mesh = new (std::nothrow) wolf::graphics::w_mesh();
                if (!_mesh)
                {
                    logger.error("Error on allocating memory for mesh");
                    continue;
                }

                std::vector<float> _v;
                for (auto& _data : _mesh_data->vertices)
                {
                    auto _pos = _data.position;
                    auto _uv = _data.uv;

                    //position
                    _v.push_back(_pos[0]);
                    _v.push_back(_pos[1]);
                    _v.push_back(_pos[2]);

                    //uv
                    _v.push_back(_uv[0]);
                    _v.push_back(1 - _uv[1]);
                }

                auto _v_size = _v.size();
                _hr = _mesh->load(_gDevice, _v.data(), _v_size * sizeof(float), _v_size, _mesh_data->indices.data(), _mesh_data->indices.size());

                _v.clear();
                if (_hr == S_FALSE)
                {
                    logger.error("Error on creating mesh");
                    continue;
                }
            }

            //std::vector<w_cpipeline_model::w_instance_info> _model_instances;
            //_model->get_instances(_model_instances);
            //if (_model_instances.size())
            //{
            //    //update_instance_buffer(_model_instances.data(),
            //    //    static_cast<UINT>(_model_instances.size() * sizeof(content_pipeline::w_cpipeline_model::w_instance_info)),
            //    //    w_mesh::w_vertex_declaration::VERTEX_POSITION_UV_INSTANCE_VEC7_INT);
            //}

        }

        _scene->get_first_camera(this->_camera);
        this->_camera.set_far_plan(10000);
        this->_camera.set_aspect_ratio((float)_screen_size.x / (float)_screen_size.y);
        this->_camera.update_view();
        this->_camera.update_projection();

        _scene->release();
    }

    //create frame buffers
    _hr = this->_frame_buffers.load(_gDevice,
        _render_pass_handle,
        _output_window->vk_swap_chain_image_views,
        &_output_window->vk_depth_buffer_image_view,
        _screen_size,
        1);
    if (_hr == S_FALSE)
    {
        logger.error("Error on creating frame buffers");
        release();
        exit(1);
    }

    //now assign new command buffers
    this->_command_buffers.set_enable(true);
    this->_command_buffers.set_order(1);
    _hr = this->_command_buffers.load(_gDevice, _output_window->vk_swap_chain_image_views.size());
    if (_hr == S_FALSE)
    {
        logger.error("Error on creating command buffers");
        release();
        exit(1);
    }

    _hr = _gDevice->store_to_global_command_buffers("render_quad_with_texture",
        &this->_command_buffers,
        _output_window->index);
    if (_hr == S_FALSE)
    {
        logger.error("Error creating command buffer");
        return;
    }

    _output_window->command_buffers.at("clear_color_screen")->set_enable(false);

    //load image texture
    w_texture* _gui_images = new w_texture();
    _gui_images->load(_gDevice);
    _gui_images->initialize_texture_2D_from_file(content_path + L"textures/gui/icons.png", &_gui_images);
    w_imgui::load(_gDevice, _output_window->hwnd, _screen_size, _render_pass_handle, _gui_images);
}

void scene::update(_In_ const wolf::system::w_game_time& pGameTime)
{
    if (w_game::exiting) return;
    this->_camera.update(pGameTime, this->_screen_size);
     w_game::update(pGameTime);
}

static float __t = 1.0f;
HRESULT scene::render(_In_ const wolf::system::w_game_time& pGameTime)
{
    auto _gDevice = this->graphics_devices[0];
    auto _output_window = &(_gDevice->output_presentation_windows[0]);

    //record clear screen command buffer for every swap chain image
    for (uint32_t i = 0; i < this->_command_buffers.get_commands_size(); ++i)
    {
        this->_command_buffers.begin(i);
        {
            auto _cmd = this->_command_buffers.get_command_at(i);

            this->_render_pass.begin(_cmd,
                this->_frame_buffers.get_frame_buffer_at(i),
                w_color(43));
            {
                for (size_t i = 0; i < this->_models.size(); ++i)
                {
                    auto _model = this->_models[i];
                    if (!_model) continue;

                    if (_model->has_instances)
                    {
                        using namespace glm;

                        //auto _transform = _model->model_meshes->get_transform();
                        //auto _translate = translate(mat4(1.0f),
                        //    vec3(_transform.position[0], _transform.position[1], _transform.position[2]));
                        //mat4 _scale = scale(mat4x4(1.0f),
                        //    vec3(_transform.scale[0], _transform.scale[1], _transform.scale[2]));

                        //auto _world = _translate *
                        //    rotate(_transform.rotation[0], vec3(-1.0f, 0.0f, 0.0f)) *
                        //    rotate(_transform.rotation[1], vec3(0.0f, 1.0f, 0.0f)) *
                        //    rotate(_transform.rotation[2], vec3(0.0f, 0.0f, -1.0f)) *
                        //    _scale;

                        //this->_instance_wvp_unifrom[_instance_model_index]->data.world = _world;
                        //this->_instance_wvp_unifrom[_instance_model_index]->data.view_projection = this->_camera.get_projection() * this->_camera.get_view();
                        //this->_instance_wvp_unifrom[_instance_model_index]->update();

                        //this->_instance_color_unifrom[_instance_model_index]->data.color = glm::vec4(1);
                        //this->_instance_color_unifrom[_instance_model_index]->update();

                        //_instance_model_index++;

                        //this->_models[i]->render(_cmd);
                    }
                    else
                    {
                       /* using namespace glm;

                        auto _transform = _model->model_meshes->get_transform();
                        auto _translate = translate(mat4(1.0f),
                            vec3(_transform.position[0], _transform.position[1], _transform.position[2]));
                        mat4 _scale = scale(mat4x4(1.0f),
                            vec3(_transform.scale[0], _transform.scale[1], _transform.scale[2]));

                        auto _world = _translate *
                            rotate(_transform.rotation[0], vec3(-1.0f, 0.0f, 0.0f)) *
                            rotate(_transform.rotation[1], vec3(0.0f, 1.0f, 0.0f)) *
                            rotate(_transform.rotation[2], vec3(0.0f, 0.0f, -1.0f)) *
                            _scale;

                        _model->wvp_unifrom.data.world = _world;
                        _model->wvp_unifrom.data.view_projection = this->_camera.get_projection() * this->_camera.get_view();
                        _model->wvp_unifrom.update();

                        _model->color_unifrom.data.color = glm::vec4(1);
                        _model->color_unifrom.update();
                        
                        _model->tess_level_unifrom.data.tess_level_uniform = __t;
                        _model->tess_level_unifrom.update();
                        
                        auto _descriptor_set = _model->shader->get_descriptor_set();
                        _model->pipeline->bind(_cmd, &_descriptor_set);
                        _model->model_meshes->render(_cmd);*/
                    }
                }
                //make sure render all gui before loading gui_render
                w_imgui::new_frame((float)pGameTime.get_elapsed_seconds(), []()
                {
                    make_gui();
                });
                w_imgui::update_buffers(this->_render_pass);
                w_imgui::render(_cmd);
            }
            this->_render_pass.end(_cmd);
        }
        this->_command_buffers.end(i);
    }
    logger.write(std::to_string(pGameTime.get_frames_per_second()));
    return w_game::render(pGameTime);
}

void scene::on_window_resized(_In_ UINT pIndex)
{
    w_game::on_window_resized(pIndex);
}

void scene::on_device_lost()
{
    w_game::on_device_lost();
}

ULONG scene::release()
{
    if (this->get_is_released()) return 0;

    auto _gDevice = get_graphics_device();

    this->_render_pass.release();
    this->_frame_buffers.release();

    w_imgui::release();
    
    for (auto _model : this->_models)
    {
        SAFE_RELEASE(_model);
    }
    this->_models.clear();

    SAFE_RELEASE(this->_shader);
    SAFE_RELEASE(this->_pipeline);
    this->_wvp_unifrom.release();
   
    w_pipeline::release_all_pipeline_caches(_gDevice);

    return w_game::release();
}

static void make_gui()
{
    ImGuiStyle& _style = ImGui::GetStyle();
    _style.WindowPadding = ImVec2(3, 2);
    _style.WindowRounding = 0;
    _style.GrabRounding = 4;
    _style.GrabMinSize = 20;
    _style.FramePadding = ImVec2(5, 5);
    _style.Colors[2].x = 0.9098039215686275f;
    _style.Colors[2].y = 0.4431372549019608f;
    _style.Colors[2].z = 0.3176470588235294f;
    _style.Colors[2].w = 1.0f;

    static bool no_titlebar = true;
    static bool no_border = true;
    static bool no_resize = true;
    static bool no_move = true;
    static bool no_scrollbar = true;
    static bool no_collapse = true;
    static bool no_menu = true;

    ImGuiWindowFlags window_flags = 0;
    if (no_titlebar)  window_flags |= ImGuiWindowFlags_NoTitleBar;
    if (!no_border)   window_flags |= ImGuiWindowFlags_ShowBorders;
    if (no_resize)    window_flags |= ImGuiWindowFlags_NoResize;
    if (no_move)      window_flags |= ImGuiWindowFlags_NoMove;
    if (no_scrollbar) window_flags |= ImGuiWindowFlags_NoScrollbar;
    if (no_collapse)  window_flags |= ImGuiWindowFlags_NoCollapse;
    if (!no_menu)     window_flags |= ImGuiWindowFlags_MenuBar;
    if (!ImGui::Begin("ImGui Demo", 0, window_flags))
    {
        // Early out if the window is collapsed, as an optimization.
        ImGui::End();
        return;
    }

    ImGui::SetWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiSetCond_FirstUseEver);
    for (int i = 0; i < 6; i++)
    {
        ImTextureID tex_id = (void*)("#image");
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_ImageActive, ImColor(0.0f, 0.0f, 255.0f, 255.0f));
        ImGui::PushStyleColor(ImGuiCol_ImageHovered, ImColor(0.0f, 0.0f, 255.0f, 155.0f));
        if (ImGui::ImageButton(tex_id, ImVec2(40, 40), ImVec2(i * 0.1, 0.0), ImVec2(i * 0.1 + 0.1f, 0.1), 0, ImColor(232, 113, 83, 255)))
        {
            __t -= 0.1f;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        ImGui::Spacing();
        ImGui::Spacing();
    }

    //ImGui::ShowTestWindow();

    ImGui::End();
}