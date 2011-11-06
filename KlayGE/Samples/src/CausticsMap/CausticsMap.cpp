#include <KlayGE/KlayGE.hpp>
#include <KlayGE/ResLoader.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/Input.hpp>
#include <KlayGE/InputFactory.hpp>
#include <KlayGE/RenderFactory.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderEffect.hpp>
#include <KlayGE/FrameBuffer.hpp>
#include <KlayGE/GraphicsBuffer.hpp>
#include <KlayGE/Math.hpp>
#include <KlayGE/SceneObjectHelper.hpp>
#include <KlayGE/RenderStateObject.hpp>
#include <KlayGE/Query.hpp>
#include <KlayGE/Light.hpp>
#include <KlayGE/UI.hpp>
#include <KlayGE/Mesh.hpp>
#include <KlayGE/RenderableHelper.hpp>
#include <KlayGE/Camera.hpp>
#include <KlayGE/PostProcess.hpp>

#include <sstream>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/typeof/typeof.hpp>

using namespace KlayGE;
using namespace KlayGE::MathLib;
using namespace std;

#include "CausticsMap.hpp"

namespace
{
	const uint32_t CAUSTICS_GRID_SIZE = 256;
	uint32_t const SHADOW_MAP_SIZE = 512;
	uint32_t const ENV_CUBE_MAP_SIZE = 512;

	enum
	{
		Position_Pass,
		Position_Normal_Front_Pass,
		Position_Normal_Back_Pass,
		Single_Caustics_Pass,
		Dual_Caustics_Pass,
		Gen_Shadow_Pass,
		Refract_Pass,
		Default_Pass
	};

	enum
	{
		Exit,
	};

	InputActionDefine actions[] =
	{
		InputActionDefine(Exit, KS_Escape),
	};


	class ReceivePlane : public RenderablePlane
	{
	public:
		ReceivePlane(float length, float width)
			: RenderablePlane(length, width, 2, 2, true)
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();

			BOOST_AUTO(diffuse_loader, ASyncLoadTexture("diffuse.dds", EAH_GPU_Read));
			BOOST_AUTO(normal_loader, ASyncLoadTexture("normal.dds", EAH_GPU_Read));
			BOOST_AUTO(dist_loader, ASyncLoadTexture("distance.dds", EAH_GPU_Read));

			//normals, tangents
			uint32_t  vertex_num = rl_->NumVertices();
			vector<float3> normals(vertex_num);
			vector<float4> tangents(vertex_num);
			for (uint32_t i = 0; i < vertex_num; ++i)
			{
				normals[i] = float3(0.5f, 0.5f, 0.f);
				tangents[i] = float4(1.f, 0.5f, 0.5f, 1.f);
			}

			// convert to int
			vector<uint32_t> inormal(vertex_num);
			vector<uint32_t> itangent(vertex_num);
			ElementFormat fmt;
			if (rf.RenderEngineInstance().DeviceCaps().vertex_format_support(EF_A2BGR10))
			{
				fmt = EF_A2BGR10;

				for (uint32_t j = 0; j < vertex_num; ++ j)
				{
					inormal[j] = MathLib::clamp<uint32_t>(static_cast<uint32_t>(normals[j].x() * 1023), 0, 1023)
						| (MathLib::clamp<uint32_t>(static_cast<uint32_t>(normals[j].y() * 1023), 0, 1023) << 10)
						| (MathLib::clamp<uint32_t>(static_cast<uint32_t>(normals[j].z() * 1023), 0, 1023) << 20);
				}
				for (uint32_t j = 0; j < vertex_num; ++ j)
				{
					itangent[j] = MathLib::clamp<uint32_t>(static_cast<uint32_t>(tangents[j].x() * 1023), 0, 1023)
						| (MathLib::clamp<uint32_t>(static_cast<uint32_t>(tangents[j].y() * 1023), 0, 1023) << 10)
						| (MathLib::clamp<uint32_t>(static_cast<uint32_t>(tangents[j].z() * 1023), 0, 1023) << 20)
						| (MathLib::clamp<uint32_t>(static_cast<uint32_t>(tangents[j].w() * 3), 0, 3) << 30);
				}
			}
			else
			{
				BOOST_ASSERT(rf.RenderEngineInstance().DeviceCaps().vertex_format_support(EF_ARGB8));

				fmt = EF_ARGB8;

				for (uint32_t j = 0; j < vertex_num; ++ j)
				{
					inormal[j] = (MathLib::clamp<uint32_t>(static_cast<uint32_t>(normals[j].x() * 255), 0, 255) << 16)
						| (MathLib::clamp<uint32_t>(static_cast<uint32_t>(normals[j].y() * 255), 0, 255) << 8)
						| (MathLib::clamp<uint32_t>(static_cast<uint32_t>(normals[j].z() * 255), 0, 255) << 0);
				}
				for (uint32_t j = 0; j < vertex_num; ++ j)
				{
					itangent[j] = (MathLib::clamp<uint32_t>(static_cast<uint32_t>(tangents[j].x() * 255), 0, 255) << 16)
						| (MathLib::clamp<uint32_t>(static_cast<uint32_t>(tangents[j].y() * 255), 0, 255) << 8)
						| (MathLib::clamp<uint32_t>(static_cast<uint32_t>(tangents[j].z() * 255), 0, 255) << 0)
						| (MathLib::clamp<uint32_t>(static_cast<uint32_t>(tangents[j].w() * 255), 0, 255) << 24);
				}
			}

			ElementInitData init_data;
			init_data.row_pitch = vertex_num * sizeof(inormal[0]);
			init_data.slice_pitch = 0;
			init_data.data = &inormal[0];
			GraphicsBufferPtr normals_vb = rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read, &init_data);
			rl_->BindVertexStream(normals_vb, boost::make_tuple(vertex_element(VEU_Normal, 0, fmt)));

			init_data.row_pitch = vertex_num * sizeof(itangent[0]);
			init_data.slice_pitch = 0;
			init_data.data = &itangent[0];
			GraphicsBufferPtr tangents_vb = rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read, &init_data);
			rl_->BindVertexStream(tangents_vb, boost::make_tuple(vertex_element(VEU_Tangent, 0, fmt)));

			RenderEffectPtr effect = rf.LoadEffect("Scene.fxml");
			technique_ = effect->TechniqueByName("DistanceMapping2a");
			default_tech_ = technique_;
			if (!technique_->Validate())
			{
				technique_ = effect->TechniqueByName("DistanceMapping20");
				BOOST_ASSERT(technique_->Validate());
			}
			*(technique_->Effect().ParameterByName("diffuse_tex")) = diffuse_loader();
			*(technique_->Effect().ParameterByName("normal_tex")) = normal_loader();
			*(technique_->Effect().ParameterByName("distance_tex")) = dist_loader();

			effect = rf.LoadEffect("Scene.fxml");
			position_pass_ = effect->TechniqueByName("PositionTex");
			BOOST_ASSERT(position_pass_->Validate());

			effect = rf.LoadEffect("ShadowCubeMap.fxml");
			gen_cube_sm_tech_ = effect->TechniqueByName("GenCubeShadowMap");
			BOOST_ASSERT(gen_cube_sm_tech_->Validate());
		}

		void BindLight(LightSourcePtr light)
		{
			light_ = light;
		}

		void CausticsLight(LightSourcePtr light)
		{
			caustics_light_ = light;
		}

		void SetSMTexture(TexturePtr sm_texture)
		{
			sm_texture_ = sm_texture;
		}

		void SetCausticsMap(TexturePtr caustics_map)
		{
			caustics_map_ = caustics_map;
		}

		void Pass(uint32_t pass)
		{
			pass_ = pass;
			if (Position_Pass == pass_)
			{
				technique_ = position_pass_;
			}
			else if (Gen_Shadow_Pass == pass_)
			{
				technique_ = gen_cube_sm_tech_;
			}
			else
			{
				technique_ = default_tech_;
			}
		}

		void OnRenderBegin()
		{
			App3DFramework const & app = Context::Instance().AppInstance();

			float4x4 const & model = MathLib::rotation_x(DEG90);
			float4x4 const & view = app.ActiveCamera().ViewMatrix();
			float4x4 const & proj = app.ActiveCamera().ProjMatrix();

			if (Position_Pass == pass_)
			{
				*(technique_->Effect().ParameterByName("mvp")) = model * view * proj;
				*(technique_->Effect().ParameterByName("model")) = model;
			}
			else if (Gen_Shadow_Pass == pass_)
			{
				float4x4 light_model = MathLib::to_matrix(light_->Rotation()) * MathLib::translation(light_->Position());
				float4x4 inv_light_model = MathLib::inverse(light_model);

				*(technique_->Effect().ParameterByName("model")) = model;
				*(technique_->Effect().ParameterByName("obj_model_to_light_model")) = model * inv_light_model;
				*(technique_->Effect().ParameterByName("mvp")) = model * view * proj;
			}
			else
			{
				float4x4 light_model = MathLib::to_matrix(light_->Rotation()) * MathLib::translation(light_->Position());
				float4x4 inv_light_model = MathLib::inverse(light_model);
				float4x4 first_light_view = light_->SMCamera(0)->ViewMatrix();

				*(technique_->Effect().ParameterByName("light_pos")) = float3(MathLib::transform(light_->Position(), MathLib::inverse(MathLib::rotation_x(DEG90))));
				*(technique_->Effect().ParameterByName("light_color")) = float3(light_->Color());
				*(technique_->Effect().ParameterByName("light_falloff")) = light_->Falloff();
				*(technique_->Effect().ParameterByName("mvp")) = model * view * proj;
				*(technique_->Effect().ParameterByName("model")) = model;
				*(technique_->Effect().ParameterByName("light_vp")) = caustics_light_->SMCamera(0)->ViewMatrix() * caustics_light_->SMCamera(0)->ProjMatrix();
				*(technique_->Effect().ParameterByName("eye_pos")) = float3(MathLib::transform(app.ActiveCamera().EyePos(), MathLib::inverse(model)));
				*(technique_->Effect().ParameterByName("shadow_cube_tex")) = sm_texture_;
				*(technique_->Effect().ParameterByName("caustics_tex")) = caustics_map_;
				*(technique_->Effect().ParameterByName("obj_model_to_light_model")) = model * inv_light_model;
				*(technique_->Effect().ParameterByName("obj_model_to_light_view")) = model * first_light_view;

				RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
				*(technique_->Effect().ParameterByName("flipping")) = static_cast<int32_t>(re.CurFrameBuffer()->RequiresFlipping() ? -1 : +1);
			}			
			
		}

	private:
		uint32_t pass_;
		RenderTechniquePtr default_tech_;
		RenderTechniquePtr position_pass_;
		RenderTechniquePtr gen_cube_sm_tech_;
		LightSourcePtr light_;
		LightSourcePtr caustics_light_;
		TexturePtr sm_texture_;
		TexturePtr caustics_map_;
	};

	class PlaneObject : public SceneObjectHelper
	{
	public:
		PlaneObject(float length, float width)
			: SceneObjectHelper(MakeSharedPtr<ReceivePlane>(length, width), SOA_Moveable)
		{
		}

		void Pass(uint32_t pass)
		{
			checked_pointer_cast<ReceivePlane>(renderable_)->Pass(pass);
		}
	};

	class RefractMesh : public StaticMesh
	{
	public:
		RefractMesh(RenderModelPtr const & model, std::wstring const & name)
			: StaticMesh(model, name)
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();

			RenderEffectPtr effect = rf.LoadEffect("Scene.fxml");
			caustics_input_tech_f_ = effect->TechniqueByName("PosNormTexFront");
			BOOST_ASSERT(caustics_input_tech_f_->Validate());
			caustics_input_tech_b_ = effect->TechniqueByName("PosNormTexBack");
			BOOST_ASSERT(caustics_input_tech_b_->Validate());

			refract_tech_ = effect->TechniqueByName("RefractEffect");
			BOOST_ASSERT(refract_tech_->Validate());

			effect = rf.LoadEffect("ShadowCubeMap.fxml");
			gen_cube_sm_tech_ = effect->TechniqueByName("GenCubeShadowMap");
			BOOST_ASSERT(gen_cube_sm_tech_->Validate());
		}

		void BindLight(LightSourcePtr const & light)
		{
			light_ = light;
		}

		void SceneTexture(TexturePtr const & scene_texture)
		{
			scene_texture_ = scene_texture;
		}

		void SetEnvCubeCamera(CameraPtr const & camera)
		{
			env_cube_camera_ = camera;
		}

		void SetEnvCube(TexturePtr const & texture)
		{
			env_cube_ = texture;
		}

		void OnRenderBegin()
		{
			CausticsMapApp& app = *checked_cast<CausticsMapApp*>(&Context::Instance().AppInstance());

			float4x4 const & view = app.ActiveCamera().ViewMatrix();
			float4x4 const & proj = app.ActiveCamera().ProjMatrix();

			switch (pass_)
			{
			case Position_Normal_Front_Pass:
			case Position_Normal_Back_Pass:
				{
					*(technique_->Effect().ParameterByName("mvp")) = model_mat_ * view * proj;
					*(technique_->Effect().ParameterByName("model")) = model_mat_;
				}
				break;

			case Gen_Shadow_Pass:
				{
					float4x4 light_model = MathLib::to_matrix(light_->Rotation()) * MathLib::translation(light_->Position());
					float4x4 inv_light_model = MathLib::inverse(light_model);

					*(technique_->Effect().ParameterByName("model")) = model_mat_;
					*(technique_->Effect().ParameterByName("obj_model_to_light_model")) = model_mat_ * inv_light_model;
					*(technique_->Effect().ParameterByName("mvp")) = model_mat_ * view * proj;
				}
				break;

			case Refract_Pass:
				{
					float refract_idx = app.RefractIndex();
					float3 absorption_idx = float3(0.1f, 0.1f, 0.1f);
					
					*(technique_->Effect().ParameterByName("mvp")) = model_mat_ * view * proj;
					*(technique_->Effect().ParameterByName("model")) = model_mat_;
					*(technique_->Effect().ParameterByName("vp")) = view * proj;
					*(technique_->Effect().ParameterByName("eye_pos")) = app.ActiveCamera().EyePos();				
					*(technique_->Effect().ParameterByName("background_texture")) = scene_texture_;
					*(technique_->Effect().ParameterByName("env_cube")) = env_cube_;
					*(technique_->Effect().ParameterByName("refract_idx")) = float2(refract_idx, 1.0f / refract_idx);
					*(technique_->Effect().ParameterByName("absorption_idx")) = absorption_idx;

					RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
					*(technique_->Effect().ParameterByName("flipping")) = static_cast<int32_t>(re.CurFrameBuffer()->RequiresFlipping() ? -1 : +1);
				}
				break;
			}
		}

		void Pass(uint32_t pass)
		{
			pass_ = pass;

			switch (pass_)
			{
			case Position_Normal_Front_Pass:
				technique_ = caustics_input_tech_f_;
				break;

			case Position_Normal_Back_Pass:
				technique_ = caustics_input_tech_b_;
				break;

			case Gen_Shadow_Pass:
				technique_ = gen_cube_sm_tech_;
				break;

			case Refract_Pass:
				technique_ = refract_tech_;
				break;
			}
		}

	private:
		RenderTechniquePtr refract_tech_;
		RenderTechniquePtr caustics_input_tech_f_;
		RenderTechniquePtr caustics_input_tech_b_;
		RenderTechniquePtr gen_cube_sm_tech_;
		uint32_t pass_;
		LightSourcePtr light_;
		TexturePtr scene_texture_;
		CameraPtr env_cube_camera_;
		TexturePtr env_cube_;
	};

	class RefractModel : public RenderModel
	{
	public:
		explicit RefractModel(std::wstring const & name)
			: RenderModel(name)
		{
		}

		void BindLight(LightSourcePtr const & light)
		{
			BOOST_FOREACH(BOOST_TYPEOF(meshes_)::reference mesh, meshes_)
			{
				checked_pointer_cast<RefractMesh>(mesh)->BindLight(light);
			}
		}

		void SceneTexture(TexturePtr const & scene_texture)
		{
			BOOST_FOREACH(BOOST_TYPEOF(meshes_)::reference mesh, meshes_)
			{
				checked_pointer_cast<RefractMesh>(mesh)->SceneTexture(scene_texture);
			}
		}

		void SetModelMatrix(float4x4 const & model)
		{
			BOOST_FOREACH(BOOST_TYPEOF(meshes_)::reference mesh, meshes_)
			{
				checked_pointer_cast<RefractMesh>(mesh)->SetModelMatrix(model);
			}
		}

		void SetEnvCube(TexturePtr const & texture)
		{
			BOOST_FOREACH(BOOST_TYPEOF(meshes_)::reference mesh, meshes_)
			{
				checked_pointer_cast<RefractMesh>(mesh)->SetEnvCube(texture);
			}
		}

		void Pass(uint32_t pass)
		{
			BOOST_FOREACH(BOOST_TYPEOF(meshes_)::reference mesh, meshes_)
			{
				checked_pointer_cast<RefractMesh>(mesh)->Pass(pass);
			}
		}
	};

	class CausticsGrid : public RenderableHelper
	{
	public:
		CausticsGrid()
			: RenderableHelper(L"CausticsGrid")
		{
			RenderFactory& rf = Context::Instance().RenderFactoryInstance();
			
			BOOST_AUTO(point_texture_loader, ASyncLoadTexture("point.dds", EAH_GPU_Read | EAH_Immutable));
			
			std::vector<float2> xys(CAUSTICS_GRID_SIZE * CAUSTICS_GRID_SIZE);
			for (uint32_t i = 0; i < CAUSTICS_GRID_SIZE; ++ i)
			{
				for (uint32_t j = 0; j < CAUSTICS_GRID_SIZE; ++ j)
				{
					xys[i * CAUSTICS_GRID_SIZE + j] = float2(static_cast<float>(j), static_cast<float>(i)) / static_cast<float>(CAUSTICS_GRID_SIZE);
				}
			}

			rl_ = rf.MakeRenderLayout();
			rl_->TopologyType(RenderLayout::TT_PointList);

			ElementInitData init_data;
			init_data.row_pitch = xys.size() * sizeof(xys[0]);
			init_data.slice_pitch = 0;
			init_data.data = &xys[0];
			GraphicsBufferPtr point_vb = rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read, &init_data);
			rl_->BindVertexStream(point_vb, boost::make_tuple(vertex_element(VEU_Position, 0, EF_GR32F)));

			box_ = Box(float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 0.0f));

			RenderEffectPtr effect = rf.LoadEffect("Caustics.fxml");
			single_caustics_pass_ = effect->TechniqueByName("GenSingleFaceCausticsMap");
			BOOST_ASSERT(single_caustics_pass_->Validate());
			dual_caustics_pass_ = effect->TechniqueByName("GenDualFaceCausticsMap");
			BOOST_ASSERT(dual_caustics_pass_->Validate());

			point_texture_ = point_texture_loader();
		}

		void Pass(uint32_t pass)
		{
			pass_ = pass;
			if (Single_Caustics_Pass == pass_)
			{
				technique_ = single_caustics_pass_;
			}
			else if (Dual_Caustics_Pass == pass_)
			{
				technique_ = dual_caustics_pass_;
			}
		}

		void OnRenderBegin()
		{
			CausticsMapApp& app = *checked_cast<CausticsMapApp*>(&Context::Instance().AppInstance());
			Camera const & camera = app.ActiveCamera();
			float4x4 light_view = camera.ViewMatrix();
			float4x4 light_proj = camera.ProjMatrix();
			float pt_size = app.PointSize();
			float refract_idx = app.RefractIndex();
			float3 absorption_idx = float3(0.1f, 0.1f, 0.1f);
			uint32_t occlusion_pixs = static_cast<uint32_t>(app.OcclusionQueryResult());

			float3 light_pos = app.GetLightSource()->Position();
			float3 light_clr = app.GetLightSource()->Color();
			float light_density = app.LightDensity();

			CausticsInputTexture const & input_tex = app.GetCausticsInputTexture();

			if ((Single_Caustics_Pass == pass_) || (Dual_Caustics_Pass == pass_))
			{			
				*(technique_->Effect().ParameterByName("light_view")) = light_view;
				*(technique_->Effect().ParameterByName("light_proj")) = light_proj;
				*(technique_->Effect().ParameterByName("light_vp")) = light_view * light_proj;
				*(technique_->Effect().ParameterByName("light_pos")) = light_pos;
				*(technique_->Effect().ParameterByName("light_color")) = light_clr;
				*(technique_->Effect().ParameterByName("light_density")) = light_density;
				*(technique_->Effect().ParameterByName("point_size")) = pt_size;
				*(technique_->Effect().ParameterByName("pt_texture")) = point_texture_;
				*(technique_->Effect().ParameterByName("refract_idx")) = float2(refract_idx, 1.0f / refract_idx);
				*(technique_->Effect().ParameterByName("absorption_idx")) = absorption_idx;
				*(technique_->Effect().ParameterByName("inv_occlusion_pixs")) = 1.0f / occlusion_pixs;

				float q = camera.FarPlane() / (camera.FarPlane() - camera.NearPlane());
				*(technique_->Effect().ParameterByName("near_q")) = float2(camera.NearPlane() * q, q);

				*(technique_->Effect().ParameterByName("t_background_depth")) = input_tex.background_depth_tex;
				*(technique_->Effect().ParameterByName("t_first_depth")) = input_tex.refract_obj_depth_tex_f;
				*(technique_->Effect().ParameterByName("t_first_normals")) = input_tex.refract_obj_N_texture_f;

				float4x4 inv_light_view = MathLib::inverse(light_view);
				float4x4 inv_light_proj = MathLib::inverse(light_proj);
				float3 ttow_center = MathLib::transform_normal(MathLib::transform_coord(float3(0, 0, 1), inv_light_proj), inv_light_view);
				float3 ttow_left = MathLib::transform_coord(MathLib::transform_coord(float3(-1, 0, 1), inv_light_proj), inv_light_view);
				float3 ttow_right = MathLib::transform_coord(MathLib::transform_coord(float3(+1, 0, 1), inv_light_proj), inv_light_view);
				float3 ttow_upper = MathLib::transform_coord(MathLib::transform_coord(float3(0, +1, 1), inv_light_proj), inv_light_view);
				float3 ttow_lower = MathLib::transform_coord(MathLib::transform_coord(float3(0, -1, 1), inv_light_proj), inv_light_view);
				*(technique_->Effect().ParameterByName("ttow_z")) = float4(ttow_center.x(), ttow_center.y(), ttow_center.z(), MathLib::length(ttow_center));
				*(technique_->Effect().ParameterByName("ttow_x")) = ttow_right - ttow_left;
				*(technique_->Effect().ParameterByName("ttow_y")) = ttow_lower - ttow_upper;

				if (Dual_Caustics_Pass == pass_)
				{
					*(technique_->Effect().ParameterByName("t_second_depth")) = input_tex.refract_obj_depth_tex_b;
					*(technique_->Effect().ParameterByName("t_second_normals")) = input_tex.refract_obj_N_texture_b;
				}
			}

			RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();
			*(technique_->Effect().ParameterByName("flipping")) = static_cast<int32_t>(re.CurFrameBuffer()->RequiresFlipping() ? -1 : +1);
		}

	private:
		uint32_t pass_;
		RenderTechniquePtr single_caustics_pass_;
		RenderTechniquePtr dual_caustics_pass_;
		TexturePtr point_texture_;
	};
}

int main()
{
	ResLoader::Instance().AddPath("../../Samples/media/Common");

	Context::Instance().LoadCfg("KlayGE.cfg");

	CausticsMapApp app;
	app.Create();
	app.Run();

	return 0;
}

CausticsMapApp::CausticsMapApp()
	: App3DFramework("Caustics Map")
{
	ResLoader::Instance().AddPath("../../Samples/media/DistanceMapping");
	ResLoader::Instance().AddPath("../../Samples/media/ShadowCubeMap");
	ResLoader::Instance().AddPath("../../Samples/media/CausticsMap");
}

bool CausticsMapApp::ConfirmDevice() const
{
	return true;
}

void CausticsMapApp::InitObjects()
{
	RenderFactory& rf = Context::Instance().RenderFactoryInstance();

	//Font config
	font_ = rf.MakeFont("gkai00mp.kfont");

	//Default Camera
	this->LookAt(float3(30, 30, -30), float3(0.0f, 0.0f, 0.0f));
	this->Proj(0.1f, 100);
	trackball_controller_.AttachCamera(this->ActiveCamera());
	trackball_controller_.Scalers(0.05f, 0.1f);

	//Light
	light_ = MakeSharedPtr<SpotLightSource>();
	light_->Attrib(0);
	light_->Color(float3(10, 10, 10));
	light_->Falloff(float3(1, 0, 0.01f));
	light_->Position(float3(0, 30, 0));
	light_->Direction(MathLib::normalize(-light_->Position()));
	light_->OuterAngle(PI / 8);
	light_->AddToSceneManager();

	//dummy light for Shadow
	dummy_light_ = MakeSharedPtr<PointLightSource>();
	dummy_light_->Attrib(0);
	dummy_light_->Color(light_->Color());
	dummy_light_->Falloff(light_->Falloff());
	dummy_light_->Position(light_->Position());

	//for env map generating
	dummy_light_env_ = MakeSharedPtr<PointLightSource>();
	dummy_light_env_->Position(float3(0.0f, 20.0f, 0.0f));

	light_proxy_ = MakeSharedPtr<SceneObjectLightSourceProxy>(light_);
	light_proxy_->AddToSceneManager();

	//Input Bind
	InputEngine& ie(Context::Instance().InputFactoryInstance().InputEngineInstance());
	InputActionMap action_map;
	action_map.AddActions(actions, actions + sizeof(actions) / sizeof(actions[0]));
	action_handler_t input_handler = MakeSharedPtr<input_signal>();
	input_handler->connect(boost::bind(&CausticsMapApp::InputHandler, this, _1, _2));
	ie.ActionMap(action_map, input_handler, true);

	//Model
	plane_object_ = MakeSharedPtr<PlaneObject>(50.f, 50.f);
	plane_object_->AddToSceneManager();

	RenderablePtr model_sphere = SyncLoadModel("sphere_high.7z//sphere_high.meshml", EAH_GPU_Read | EAH_Immutable,
		CreateModelFactory<RefractModel>(), CreateMeshFactory<RefractMesh>());
	checked_pointer_cast<RefractModel>(model_sphere)->SetModelMatrix(MathLib::scaling(200.0f, 200.0f, 200.0f) * MathLib::translation(0.0f, 10.0f, 0.0f));
	sphere_ = MakeSharedPtr<SceneObjectHelper>(model_sphere, SceneObjectHelper::SOA_Cullable);
	sphere_->AddToSceneManager();
	sphere_->Visible(false);

	RenderablePtr model_bunny = SyncLoadModel("bunny.7z//bunny.meshml", EAH_GPU_Read | EAH_Immutable,
		CreateModelFactory<RefractModel>(), CreateMeshFactory<RefractMesh>());
	checked_pointer_cast<RefractModel>(model_bunny)->SetModelMatrix(MathLib::scaling(320.0f, 320.0f, 320.0f) * MathLib::translation(3.0f, 2.0f, 0.0f));
	bunny_ = MakeSharedPtr<SceneObjectHelper>(model_bunny, SceneObjectHelper::SOA_Cullable);
	bunny_->AddToSceneManager();
	bunny_->Visible(false);

	refract_obj_ = sphere_;

	caustics_grid_ = MakeSharedPtr<CausticsGrid>();

	//Sky Box
	y_cube_map_ = SyncLoadTexture("uffizi_cross_y.dds", EAH_GPU_Read);
	c_cube_map_ = SyncLoadTexture("uffizi_cross_c.dds", EAH_GPU_Read);
	sky_box_ = MakeSharedPtr<SceneObjectHDRSkyBox>(0);
	checked_pointer_cast<SceneObjectHDRSkyBox>(sky_box_)->CompressedCubeMap(y_cube_map_, c_cube_map_);
	sky_box_->AddToSceneManager();

	copy_pp_ = LoadPostProcess(ResLoader::Instance().Open("Copy.ppml"), "copy");

	this->InitUI();

	this->InitBuffer();

	this->InitCubeSM();

	this->InitEnvCube();

	caustics_map_pps_ = MakeSharedPtr<BlurPostProcess<SeparableGaussianFilterPostProcess> >(5, 1.0f);
	caustics_map_pps_->InputPin(0, caustics_texture_);
	caustics_map_pps_->OutputPin(0, caustics_texture_filtered_);

	occlusion_query_ = checked_pointer_cast<OcclusionQuery>(rf.MakeOcclusionQuery());
}

void CausticsMapApp::InitBuffer()
{
	RenderFactory& rf = Context::Instance().RenderFactoryInstance();
	RenderEngine& re = rf.RenderEngineInstance();
	
	ElementFormat fmt;
	if (re.DeviceCaps().rendertarget_format_support(EF_A2BGR10, 1, 0))
	{
		fmt = EF_A2BGR10;
	}
	else
	{
		if (re.DeviceCaps().rendertarget_format_support(EF_ABGR8, 1, 0))
		{
			fmt = EF_ABGR8;
		}
		else
		{
			fmt = EF_ARGB8;
		}
	}

	refract_obj_N_texture_f_ = rf.MakeTexture2D(CAUSTICS_GRID_SIZE, CAUSTICS_GRID_SIZE, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	refract_obj_N_texture_b_ = rf.MakeTexture2D(CAUSTICS_GRID_SIZE, CAUSTICS_GRID_SIZE, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	refract_obj_depth_tex_f_ = rf.MakeTexture2D(CAUSTICS_GRID_SIZE, CAUSTICS_GRID_SIZE, 1, 1, EF_D16, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	refract_obj_depth_tex_b_ = rf.MakeTexture2D(CAUSTICS_GRID_SIZE, CAUSTICS_GRID_SIZE, 1, 1, EF_D16, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);

	if (re.DeviceCaps().rendertarget_format_support(EF_ABGR8, 1, 0))
	{
		fmt = EF_ABGR8;
	}
	else
	{
		fmt = EF_ARGB8;
	}
	TexturePtr dummy_tex = rf.MakeTexture2D(CAUSTICS_GRID_SIZE, CAUSTICS_GRID_SIZE, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	background_depth_tex_ = rf.MakeTexture2D(CAUSTICS_GRID_SIZE, CAUSTICS_GRID_SIZE, 1, 1, EF_D16, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);

	background_fb_ = rf.MakeFrameBuffer();
	background_fb_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*dummy_tex, 0, 1, 0));
	background_fb_->Attach(FrameBuffer::ATT_DepthStencil, rf.Make2DDepthStencilRenderView(*background_depth_tex_, 0, 1, 0));

	refract_obj_fb_f_ = rf.MakeFrameBuffer();
	refract_obj_fb_f_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*refract_obj_N_texture_f_, 0, 1, 0));
	refract_obj_fb_f_->Attach(FrameBuffer::ATT_DepthStencil, rf.Make2DDepthStencilRenderView(*refract_obj_depth_tex_f_, 0, 1, 0));
	
	refract_obj_fb_b_ = rf.MakeFrameBuffer();
	refract_obj_fb_b_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*refract_obj_N_texture_b_, 0, 1, 0));
	refract_obj_fb_b_->Attach(FrameBuffer::ATT_DepthStencil, rf.Make2DDepthStencilRenderView(*refract_obj_depth_tex_b_, 0, 1, 0));

	caustics_texture_ = rf.MakeTexture2D(CAUSTICS_GRID_SIZE, CAUSTICS_GRID_SIZE, 1, 1, EF_ABGR16F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	caustics_texture_filtered_ = rf.MakeTexture2D(CAUSTICS_GRID_SIZE, CAUSTICS_GRID_SIZE, 1, 1, EF_ABGR16F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	caustics_fb_ = rf.MakeFrameBuffer();
	caustics_fb_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*caustics_texture_, 0, 1, 0));

	scene_fb_ = rf.MakeFrameBuffer();
	scene_fb_->GetViewport().camera = re.DefaultFrameBuffer()->GetViewport().camera;
}

void CausticsMapApp::InitEnvCube()
{
	RenderFactory& rf = Context::Instance().RenderFactoryInstance();

	TexturePtr env_cube_depth_tex = rf.MakeTextureCube(ENV_CUBE_MAP_SIZE, 1, 1, EF_D24S8, 1, 0, EAH_GPU_Write, NULL);
	if (rf.RenderEngineInstance().DeviceCaps().rendertarget_format_support(EF_B10G11R11F, 1, 0))
	{
		env_cube_tex_ = rf.MakeTextureCube(ENV_CUBE_MAP_SIZE, 1, 1, EF_B10G11R11F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	}
	else
	{
		env_cube_tex_ = rf.MakeTextureCube(ENV_CUBE_MAP_SIZE, 1, 1, EF_ABGR16F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	}

	for (int i = 0; i < 6; ++ i)
	{
		env_cube_rvs_[i] = rf.Make2DRenderView(*env_cube_tex_, 0, static_cast<Texture::CubeFaces>(i), 0);
		env_cube_depth_rvs_[i] = rf.Make2DDepthStencilRenderView(*env_cube_depth_tex, 0, static_cast<Texture::CubeFaces>(i), 0);

		env_cube_buffers_[i] = rf.MakeFrameBuffer();
		env_cube_buffers_[i]->Attach(FrameBuffer::ATT_Color0, env_cube_rvs_[i]);
		env_cube_buffers_[i]->Attach(FrameBuffer::ATT_DepthStencil, env_cube_depth_rvs_[i]);
		env_cube_buffers_[i]->GetViewport().camera = dummy_light_env_->SMCamera(i);
	}
}

void CausticsMapApp::InitCubeSM()
{	
	RenderFactory& rf = Context::Instance().RenderFactoryInstance();

	RenderViewPtr depth_view = rf.Make2DDepthStencilRenderView(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, EF_D24S8, 1, 0);
	shadow_cube_buffer_ = rf.MakeFrameBuffer();
	ElementFormat fmt;
	if (rf.RenderEngineInstance().DeviceCaps().rendertarget_format_support(EF_GR16F, 1, 0))
	{
		fmt = EF_GR16F;
	}
	else
	{
		BOOST_ASSERT(rf.RenderEngineInstance().DeviceCaps().rendertarget_format_support(EF_ABGR16F, 1, 0));

		fmt = EF_ABGR16F;
	}
	shadow_tex_ = rf.MakeTexture2D(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	shadow_cube_buffer_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*shadow_tex_, 0, 0, 0));
	shadow_cube_buffer_->Attach(FrameBuffer::ATT_DepthStencil, depth_view);

	shadow_cube_tex_ = rf.MakeTextureCube(SHADOW_MAP_SIZE, 1, 1, shadow_tex_->Format(), 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);

	for (int i = 0; i < 6; ++ i)
	{
		sm_filter_pps_[i] = MakeSharedPtr<BlurPostProcess<SeparableGaussianFilterPostProcess> >(3, 1.0f);

		sm_filter_pps_[i]->InputPin(0, shadow_tex_);
		sm_filter_pps_[i]->OutputPin(0, shadow_cube_tex_, 0, 0, i);
		if (!shadow_cube_buffer_->RequiresFlipping())
		{
			switch (i)
			{
			case Texture::CF_Positive_Y:
				sm_filter_pps_[i]->OutputPin(0, shadow_cube_tex_, 0, 0, Texture::CF_Negative_Y);
				break;

			case Texture::CF_Negative_Y:
				sm_filter_pps_[i]->OutputPin(0, shadow_cube_tex_, 0, 0, Texture::CF_Positive_Y);
				break;

			default:
				break;
			}
		}
	}
}

void CausticsMapApp::InitUI()
{
	//UI Settings
	UIManager::Instance().Load(ResLoader::Instance().Open("Caustics.uiml"));
	dialog_ = UIManager::Instance().GetDialogs()[0];

	int ui_id = 0;
	ui_id = dialog_->IDFromName("Light_Density_Slider");
	dialog_->Control<UISlider>(ui_id)->OnValueChangedEvent().connect(boost::bind(&CausticsMapApp::LightDensityHandler, this, _1));
	LightDensityHandler(*(dialog_->Control<UISlider>(ui_id)));

	ui_id = dialog_->IDFromName("Refraction_Index_Slider");
	dialog_->Control<UISlider>(ui_id)->OnValueChangedEvent().connect(boost::bind(&CausticsMapApp::RefractIndexHandler, this, _1));
	RefractIndexHandler(*(dialog_->Control<UISlider>(ui_id)));

	ui_id = dialog_->IDFromName("Point_Size_Slider");
	dialog_->Control<UISlider>(ui_id)->OnValueChangedEvent().connect(boost::bind(&CausticsMapApp::PointSizeHandler, this, _1));
	PointSizeHandler(*(dialog_->Control<UISlider>(ui_id)));

	ui_id = dialog_->IDFromName("Dual_Caustics_Checkbox");
	dialog_->Control<UICheckBox>(ui_id)->OnChangedEvent().connect(boost::bind(&CausticsMapApp::DualFaceCausticsCheckBoxHandler, this, _1));
	DualFaceCausticsCheckBoxHandler(*(dialog_->Control<UICheckBox>(ui_id)));

	ui_id = dialog_->IDFromName("Model_Combobox");
	dialog_->Control<UIComboBox>(ui_id)->OnSelectionChangedEvent().connect(boost::bind(&CausticsMapApp::ModelSelectionComboBox, this, _1));
	ModelSelectionComboBox(*(dialog_->Control<UIComboBox>(ui_id)));
}

void CausticsMapApp::OnResize(uint32_t width, uint32_t height)
{
	App3DFramework::OnResize(width, height);

	UIManager::Instance().SettleCtrls(width, height);

	RenderFactory& rf = Context::Instance().RenderFactoryInstance();
	RenderEngine& re = rf.RenderEngineInstance();

	ElementFormat fmt;
	if (re.DeviceCaps().rendertarget_format_support(EF_B10G11R11F, 1, 0))
	{
		fmt = EF_B10G11R11F;
	}
	else
	{
		if (re.DeviceCaps().rendertarget_format_support(EF_ABGR8, 1, 0))
		{
			fmt = EF_ABGR8;
		}
		else
		{
			fmt = EF_ARGB8;
		}
	}
	scene_texture_ = rf.MakeTexture2D(width, height, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, NULL);
	scene_fb_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*scene_texture_, 0, 0, 0));
	scene_fb_->Attach(FrameBuffer::ATT_DepthStencil, rf.Make2DDepthStencilRenderView(width, height, EF_D24S8, 1, 0));

	copy_pp_->InputPin(0, scene_texture_);
}

void CausticsMapApp::InputHandler(InputEngine const & /*sender*/, InputAction const & action)
{
	switch (action.first)
	{
	case Exit:
		this->Quit();
		break;
	}
}

//UI Handler
void CausticsMapApp::RefractIndexHandler(KlayGE::UISlider const & sender)
{
	float idx_min = 1.0f;
	float idx_max = 2.0f;

	int min_val, max_val;
	sender.GetRange(min_val, max_val);

	refract_idx_ = (static_cast<float>(sender.GetValue()) - min_val) / (max_val - min_val) * (idx_max - idx_min) + idx_min;
}

void CausticsMapApp::LightDensityHandler(KlayGE::UISlider const & sender)
{
	float density_min = 1000.0f;
	float density_max = 20000.0f;

	int min_val, max_val;
	sender.GetRange(min_val, max_val);

	light_density_ = (static_cast<float>(sender.GetValue()) - min_val) / (max_val - min_val) * (density_max - density_min) + density_min;
}

void CausticsMapApp::PointSizeHandler(KlayGE::UISlider const & sender)
{
	float pt_min = 0.01f;
	float pt_max = 0.1f;

	int min_val, max_val;
	sender.GetRange(min_val, max_val);

	point_size_ = (static_cast<float>(sender.GetValue()) - min_val) / (max_val - min_val) * (pt_max - pt_min) + pt_min;
}

void CausticsMapApp::DualFaceCausticsCheckBoxHandler(KlayGE::UICheckBox const & sender)
{
	enable_dual_face_caustics_ = sender.GetChecked();
}

void CausticsMapApp::ModelSelectionComboBox(KlayGE::UIComboBox const & sender)
{
	switch (sender.GetSelectedIndex())
	{
	case 0:
		refract_obj_->Visible(false);
		refract_obj_ = sphere_;
		refract_obj_->Visible(true);
		break;

	default:
		refract_obj_->Visible(false);
		refract_obj_ = bunny_;
		refract_obj_->Visible(true);
		break;
	}

	dummy_light_env_->Position(MathLib::transform_coord(refract_obj_->GetBound().Center(), refract_obj_->GetModelMatrix()));
}

void CausticsMapApp::DoUpdateOverlay()
{
	UIManager::Instance().Render();
	RenderEngine& re = Context::Instance().RenderFactoryInstance().RenderEngineInstance();

	wostringstream st;
	st.precision(2);
	st << fixed << this->FPS() << " FPS";

	font_->RenderText(0, 0, Color(1, 1, 0, 1), L"Caustics Map", 16);
	font_->RenderText(0, 16, Color(1, 1, 0, 1), st.str().c_str(), 16);
	font_->RenderText(0, 32, Color(1, 1, 0, 1), re.ScreenFrameBuffer()->Description(), 16);
}

uint32_t CausticsMapApp::DoUpdate(uint32_t pass)
{
	RenderFactory& rf = Context::Instance().RenderFactoryInstance();
	RenderEngine& re = rf.RenderEngineInstance();

	sky_box_->Visible(false);
	refract_obj_->Visible(false);

	uint32_t sm_start_pass = 4;
	uint32_t env_start_pass = 10;

	switch(pass)
	{
	//Pass 0 ~ 3 Caustics Map
	case 0:
		{
			checked_pointer_cast<PlaneObject>(plane_object_)->Pass(Position_Pass);

			re.BindFrameBuffer(background_fb_);
			re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth, Color(0.0f, 0.0f, 0.0f, 0.0f), 1.0f, 0);
			re.CurFrameBuffer()->GetViewport().camera = light_->SMCamera(0);
		}
		return App3DFramework::URV_Need_Flush;

	case 1:
		{
			plane_object_->Visible(false);
			refract_obj_->Visible(true);
			checked_pointer_cast<RefractModel>(refract_obj_->GetRenderable())->Pass(Position_Normal_Front_Pass);

			re.BindFrameBuffer(refract_obj_fb_f_);
			re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth, Color(0.0f, 0.0f, 0.0f, 0.0f), 1.0f, 0);
			re.CurFrameBuffer()->GetViewport().camera = light_->SMCamera(0);

			occlusion_query_->Begin();
		}
		return App3DFramework::URV_Need_Flush;

	case 2:
		{
			occlusion_query_->End();

			if (enable_dual_face_caustics_)
			{
				refract_obj_->Visible(true);
				checked_pointer_cast<RefractModel>(refract_obj_->GetRenderable())->Pass(Position_Normal_Back_Pass);

				re.BindFrameBuffer(refract_obj_fb_b_);
				re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth, Color(0.0f, 0.0f, 0.0f, 0.0f), 1.0f, 0);
				re.CurFrameBuffer()->GetViewport().camera = light_->SMCamera(0);
				return App3DFramework::URV_Need_Flush;
			}
		}
		return App3DFramework::URV_Flushed;

	case 3:
		{
			if (enable_dual_face_caustics_)
			{
				checked_pointer_cast<CausticsGrid>(caustics_grid_)->Pass(Dual_Caustics_Pass);
			}
			else
			{
				checked_pointer_cast<CausticsGrid>(caustics_grid_)->Pass(Single_Caustics_Pass);
			}

			re.BindFrameBuffer(caustics_fb_);
			re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth, Color(0.0f, 0.0f, 0.0f, 0.0f), 1.0f, 0);
			re.CurFrameBuffer()->GetViewport().camera = light_->SMCamera(0);

			occlusion_pixs_ = occlusion_query_->SamplesPassed();

			caustics_grid_->Render();
			caustics_map_pps_->Apply();
		}
		return App3DFramework::URV_Flushed;

	//Shadow Cube Map Passes
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
		{
			if (pass > sm_start_pass)
			{
				sm_filter_pps_[pass - sm_start_pass - 1]->Apply();
			}

			refract_obj_->Visible(true);
			plane_object_->Visible(true);

			checked_pointer_cast<PlaneObject>(plane_object_)->Pass(Gen_Shadow_Pass);
			checked_pointer_cast<ReceivePlane>(plane_object_->GetRenderable())->BindLight(dummy_light_);
			checked_pointer_cast<RefractModel>(refract_obj_->GetRenderable())->Pass(Gen_Shadow_Pass);
			checked_pointer_cast<RefractModel>(refract_obj_->GetRenderable())->BindLight(dummy_light_);

			re.BindFrameBuffer(shadow_cube_buffer_);
			re.CurFrameBuffer()->Attached(FrameBuffer::ATT_DepthStencil)->ClearDepth(1.0f);
			shadow_cube_buffer_->GetViewport().camera = dummy_light_->SMCamera(pass - sm_start_pass);
		}
		return App3DFramework::URV_Need_Flush;

	// env map for reflect
	case 10:
	case 11:
	case 12:
	case 13:
	case 14:
	case 15:
		{
			refract_obj_->Visible(false);
			plane_object_->Visible(true);
			sky_box_->Visible(true);

			checked_pointer_cast<PlaneObject>(plane_object_)->Pass(Default_Pass);
			checked_pointer_cast<ReceivePlane>(plane_object_->GetRenderable())->BindLight(dummy_light_);
			checked_pointer_cast<ReceivePlane>(plane_object_->GetRenderable())->CausticsLight(light_);
			checked_pointer_cast<ReceivePlane>(plane_object_->GetRenderable())->SetSMTexture(shadow_cube_tex_);
			checked_pointer_cast<ReceivePlane>(plane_object_->GetRenderable())->SetCausticsMap(caustics_texture_filtered_);

			re.BindFrameBuffer(env_cube_buffers_[pass - env_start_pass]);
			re.CurFrameBuffer()->Attached(FrameBuffer::ATT_DepthStencil)->ClearDepth(1.0f);
		}
		return App3DFramework::URV_Need_Flush;

	// render background for refract
	case 16:
		{
			refract_obj_->Visible(false);
			plane_object_->Visible(true);
			sky_box_->Visible(true);

			checked_pointer_cast<PlaneObject>(plane_object_)->Pass(Default_Pass);
			checked_pointer_cast<ReceivePlane>(plane_object_->GetRenderable())->BindLight(dummy_light_);
			checked_pointer_cast<ReceivePlane>(plane_object_->GetRenderable())->CausticsLight(light_);
			checked_pointer_cast<ReceivePlane>(plane_object_->GetRenderable())->SetSMTexture(shadow_cube_tex_);
			checked_pointer_cast<ReceivePlane>(plane_object_->GetRenderable())->SetCausticsMap(caustics_texture_filtered_);

			re.BindFrameBuffer(scene_fb_);
			re.CurFrameBuffer()->Attached(FrameBuffer::ATT_DepthStencil)->ClearDepth(1.0f);
		}
		return App3DFramework::URV_Need_Flush;

	//Scene
	default:
		{
			sky_box_->Visible(false);
			refract_obj_->Visible(true);
			plane_object_->Visible(true);

			checked_pointer_cast<PlaneObject>(plane_object_)->Pass(Default_Pass);
			checked_pointer_cast<RefractModel>(refract_obj_->GetRenderable())->Pass(Refract_Pass);
			checked_pointer_cast<RefractModel>(refract_obj_->GetRenderable())->SceneTexture(scene_texture_);
			checked_pointer_cast<RefractModel>(refract_obj_->GetRenderable())->SetEnvCube(env_cube_tex_);

			re.BindFrameBuffer(FrameBufferPtr());
			re.CurFrameBuffer()->Attached(FrameBuffer::ATT_DepthStencil)->ClearDepth(1.0f);

			copy_pp_->Apply();
		}
		return App3DFramework::URV_Need_Flush | App3DFramework::URV_Finished;
	}
}
