#include "precomp.h"

// -----------------------------------------------------------
// Initialize the renderer
// -----------------------------------------------------------
void Renderer::Init()
{
	settings = new Settings();
	settings->tracerType = KAJIYA;
	settings->antiAliasing = true;
	settings->renderBVH = false;
	tlas = new TLAS( *scene.bvh2 );
	tlas->Build();
	InitBuffers();
	InitWavefrontKernels();
	InitPostProcKernels();

	// Set initial camera focus
		camera.UpdateCamVec();
	FocusCamera( SCRWIDTH / 2, SCRHEIGHT / 2 );
}
void Renderer::Shutdown() {}
// -----------------------------------------------------------
// Main application tick function - Executed once per frame
// -----------------------------------------------------------
void Renderer::Tick( float _deltaTime )
{
	deltaTime = _deltaTime;
	// animation
#if 0
	static float animTime = 0;
	animTime += deltaTime * 0.002f;
	//scene.Animate( animTime );
	tlas->Build( );
	blasNodeBuffer->CopyToDevice( );
	tlasNodeBuffer->CopyToDevice( );
#endif
	// pixel loop
	Timer t;
	camera.UpdateCamVec();
	if ( camera.moved || imgui.reset_every_frame )
	{
		resetKernel->Run( PIXELS );
		camera.moved = false;
		settings->frames = 1;
	}
	if ( settings->renderBVH ) settings->frames = 1;
	RayTrace();
	PostProc();

	if ( imgui.show_energy_levels ) ComputeEnergy();

	settings->frames++;

	// performance report - running average - ms, MRays/s
	static float avg = 10, alpha = 1;
	avg = (1 - alpha) * avg + alpha * t.elapsed() * 1000;
	if ( alpha > 0.05f )
		alpha *= 0.5f;
	float fps = 1000 / avg, rps = (SCRWIDTH * SCRHEIGHT) * fps;
	if ( !imgui.print_performance ) return;
	printf( "%5.2fms (%.1f fps) - %.1fMrays/s\n", avg, fps, rps / 1000000 );
}
void Renderer::RayTrace()
{
	settings->numInRays = 0;
	settings->numOutRays = PIXELS;
	settings->shadowRays = 0;
	settingsBuffer->CopyToDevice();
	// generate initial primary rays
	generateKernel->SetArgument( 0, ray1Buffer );
	clSetKernelArg( generateKernel->kernel, 3, sizeof( Camera ), &camera.cam );
	generateKernel->Run( PIXELS );

	for ( int i = 0; i < MAX_BOUNCES; i++ )
	{
		extendKernel->SetArgument( 0, ray1Buffer );
		extendKernel->Run( NR_OF_PERSISTENT_THREADS );
		if ( settings->renderBVH ) break;

		shadeKernel->SetArgument( 0, ray1Buffer );
		shadeKernel->SetArgument( 1, ray2Buffer );
		shadeKernel->Run( NR_OF_PERSISTENT_THREADS );

		if ( !imgui.use_russian_roulette )
			if ( imgui.shading_type == SHADING_NEE || imgui.shading_type == SHADING_NEEIS )
				connectKernel->Run( NR_OF_PERSISTENT_THREADS );

		std::swap( ray1Buffer, ray2Buffer );
	}
	if ( imgui.use_russian_roulette )
		connectKernel->Run( NR_OF_PERSISTENT_THREADS );

}
void Renderer::PostProc()
{
	// Post processing
	Buffer* src = swap1Buffer;
	Buffer* dst = swap2Buffer;
	// Kernel takes values from pixelbuffer and puts them in src
	post_prepKernel->SetArguments( accumBuffer, src, settingsBuffer );
	post_prepKernel->Run( PIXELS );
	if ( imgui.vignet_strength > 0 )
	{
		post_vignetKernel->SetArguments( src, dst, imgui.vignet_strength );
		post_vignetKernel->Run( PIXELS );
		std::swap( src, dst );
	}
	if ( imgui.gamma_strength != 1 )
	{
		post_gammaKernel->SetArguments( src, dst, imgui.gamma_strength );
		post_gammaKernel->Run( PIXELS );
		std::swap( src, dst );
	}
	if ( imgui.chromatic_strength > 0 )
	{
		post_chromaticKernel->SetArguments( src, dst, imgui.chromatic_strength );
		post_chromaticKernel->Run( PIXELS );
		std::swap( src, dst );
	}
	// Kernel takes values from src and writes them to screen texture
	displayKernel->SetArgument( 0, src );
	displayKernel->Run( PIXELS );
}

void Renderer::ComputeEnergy()
{
	accumBuffer->CopyFromDevice();
	float4* pixels = (float4*)(accumBuffer->hostBuffer);

	imgui.energy_total = 0;
	for ( int i = 0; i < PIXELS; i++ )
	{
		imgui.energy_total += pixels[i].x;
		imgui.energy_total += pixels[i].y;
		imgui.energy_total += pixels[i].z;
	}

	imgui.energy_total *= 1 / (float)(settings->frames);
}

void Renderer::InitBuffers()
{
	// data
	primBuffer = new Buffer( sizeof( Primitive ) * scene.primitives.size() );
	texBuffer = new Buffer( sizeof( float4 ) * scene.textures.size() );
	matBuffer = new Buffer( sizeof( Material ) * scene.materials.size() );
	lightBuffer = new Buffer( sizeof( uint ) * scene.lights.size() );

	// rays
	ray1Buffer = new Buffer( PIXELS * sizeof( Ray ) );
	ray2Buffer = new Buffer( PIXELS * sizeof( Ray ) );
	shadowRayBuffer = new Buffer( 4 * PIXELS * sizeof( ShadowRay ) );

	seedBuffer = new Buffer( sizeof( uint ) * PIXELS );
	settingsBuffer = new Buffer( sizeof( Settings ) );
	accumBuffer = new Buffer( 4 * 4 * PIXELS );

	// set data
	primBuffer->hostBuffer = (uint*)scene.primitives.data();
	matBuffer->hostBuffer = (uint*)scene.materials.data();
	texBuffer->hostBuffer = (uint*)scene.textures.data();
	lightBuffer->hostBuffer = (uint*)scene.lights.data();
	settingsBuffer->hostBuffer = (uint*)settings;
	seedBuffer->hostBuffer = new uint[PIXELS];
	// settings
	settings->numPrimitives = scene.primitives.size();
	settings->numLights = scene.lights.size();

	// settings
	settings->numPrimitives = scene.primitives.size();
	settings->numLights = scene.lights.size();

	blasNodeBuffer = new Buffer( sizeof( BVHInstance ) * scene.blasNodes.size() );
	blasNodeBuffer->hostBuffer = (uint*)scene.blasNodes.data();
	tlasNodeBuffer = new Buffer( sizeof( TLASNode ) * tlas->tlasNodes.size() );
	tlasNodeBuffer->hostBuffer = (uint*)tlas->tlasNodes.data();

	// BVH
	if ( imgui.bvh_type == USE_BVH4 )
	{
		bvhNodeBuffer = new Buffer( sizeof( BVHNode4 ) * scene.bvh4->Nodes().size() );
		bvhNodeBuffer->hostBuffer = (uint*)scene.bvh4->Nodes().data();
		bvhIdxBuffer = new Buffer( sizeof( uint ) * scene.bvh4->Idx().size() );
		bvhIdxBuffer->hostBuffer = (uint*)scene.bvh4->Idx().data();
	}
	else if ( imgui.bvh_type == USE_BVH2 )
	{
		bvhNodeBuffer = new Buffer( sizeof( BVHNode2 ) * scene.bvh2->bvhNodes.size() );
		bvhNodeBuffer->hostBuffer = (uint*)scene.bvh2->bvhNodes.data();
		bvhIdxBuffer = new Buffer( sizeof( uint ) * scene.bvh2->primIdx.size() );
		bvhIdxBuffer->hostBuffer = (uint*)scene.bvh2->primIdx.data();
	}

	for ( int i = 0; i < SCRHEIGHT * SCRWIDTH; i++ )
		seedBuffer->hostBuffer[i] = RandomUInt();

	bvhNodeBuffer->CopyToDevice();
	bvhIdxBuffer->CopyToDevice();
	seedBuffer->CopyToDevice();
	primBuffer->CopyToDevice();
	texBuffer->CopyToDevice();
	matBuffer->CopyToDevice();
	blasNodeBuffer->CopyToDevice();
	tlasNodeBuffer->CopyToDevice();
	if(settings->numLights > 0 )
		lightBuffer->CopyToDevice();
	settingsBuffer->CopyToDevice();
}

void Renderer::InitWavefrontKernels()
{
	std::vector<string> defines{ imgui.shading_type, imgui.sampling_type, imgui.bvh_type };
	if ( imgui.use_russian_roulette ) defines.push_back( USE_RUSSIAN_ROULETTE );
	if ( imgui.filter_fireflies ) defines.push_back( FILTER_FIREFLIES );

	// wavefront
	resetKernel = new Kernel( "src/cl/wavefront.cl", "reset", defines );
	generateKernel = new Kernel( "src/cl/wavefront.cl", "generate", defines );
	extendKernel = new Kernel( "src/cl/wavefront.cl", "extend", defines );
	shadeKernel = new Kernel( "src/cl/wavefront.cl", "shade", defines );
	connectKernel = new Kernel( "src/cl/wavefront.cl", "connect", defines );
	focusKernel = new Kernel( "src/cl/wavefront.cl", "focus", defines );

	generateKernel->SetArgument( 1, settingsBuffer );
	generateKernel->SetArgument( 2, seedBuffer );

	extendKernel->SetArgument( 1, primBuffer );
	extendKernel->SetArgument( 2, tlasNodeBuffer );
	extendKernel->SetArgument( 3, blasNodeBuffer );
	extendKernel->SetArgument( 4, bvhNodeBuffer );
	extendKernel->SetArgument( 5, bvhIdxBuffer );
	extendKernel->SetArgument( 6, accumBuffer );
	extendKernel->SetArgument( 7, settingsBuffer );

	shadeKernel->SetArgument( 2, shadowRayBuffer );
	shadeKernel->SetArgument( 3, primBuffer );
	shadeKernel->SetArgument( 4, texBuffer );
	shadeKernel->SetArgument( 5, matBuffer );
	shadeKernel->SetArgument( 6, lightBuffer );
	shadeKernel->SetArgument( 7, settingsBuffer );
	shadeKernel->SetArgument( 8, accumBuffer );
	shadeKernel->SetArgument( 9, seedBuffer );

	connectKernel->SetArgument( 0, shadowRayBuffer );
	connectKernel->SetArgument( 1, tlasNodeBuffer );
	connectKernel->SetArgument( 2, blasNodeBuffer );
	connectKernel->SetArgument( 3, bvhNodeBuffer );
	connectKernel->SetArgument( 4, bvhIdxBuffer );
	connectKernel->SetArgument( 5, primBuffer );
	connectKernel->SetArgument( 6, matBuffer );
	connectKernel->SetArgument( 7, settingsBuffer );
	connectKernel->SetArgument( 8, accumBuffer );

	resetKernel->SetArgument( 0, accumBuffer );

	focusKernel->SetArgument( 2, tlasNodeBuffer );
	focusKernel->SetArgument( 3, blasNodeBuffer );
	focusKernel->SetArgument( 4, bvhNodeBuffer );
	focusKernel->SetArgument( 5, bvhIdxBuffer );
	focusKernel->SetArgument( 6, primBuffer );
	focusKernel->SetArgument( 7, settingsBuffer );
}

void Renderer::InitPostProcKernels()
{
	// post
	post_prepKernel = new Kernel( "src/cl/postproc.cl", "prep" );
	post_vignetKernel = new Kernel( "src/cl/postproc.cl", "vignetting" );
	post_gammaKernel = new Kernel( "src/cl/postproc.cl", "gammaCorr" );
	post_chromaticKernel = new Kernel( "src/cl/postproc.cl", "chromatic" );
	displayKernel = new Kernel( "src/cl/postproc.cl", "display" );
	// screenshot
	saveImageKernel = new Kernel( "src/cl/postproc.cl", "saveImage" );

	// buffers
	swap1Buffer = new Buffer( 4 * 4 * PIXELS );
	swap2Buffer = new Buffer( 4 * 4 * PIXELS );

	// change screen so we write to opengl texture directly
	screenBuffer = new Buffer( GetRenderTarget()->ID, 0, Buffer::TARGET );
	screen = 0;

	resetKernel->SetArguments( accumBuffer );
	displayKernel->SetArguments( accumBuffer, screenBuffer );
	saveImageKernel->SetArguments( screenBuffer, swap1Buffer );
}

void Renderer::FocusCamera( int x, int y )
{
	focusKernel->SetArgument( 0, x );
	focusKernel->SetArgument( 1, y );
	clSetKernelArg( focusKernel->kernel, 8, sizeof( Camera ), &camera.cam );
	focusKernel->Run( 1 );
	settingsBuffer->CopyFromDevice();
	if ( settings->focalLength != REALLYFAR )
	{
		printf( "focalLength: %f\n", settings->focalLength );
		camera.cam.focalLength = settings->focalLength;
	}
}

void Renderer::SaveFrame( const char* file )
{
	saveImageKernel->Run( PIXELS );
	swap1Buffer->CopyFromDevice();
	SaveImageF( file, SCRWIDTH, SCRHEIGHT, (float4*)swap1Buffer->hostBuffer );
}

void Renderer::MouseMove( int x, int y, bool mouse_active )
{
	if ( mouse_active )
	{
		camera.MouseMove( x - mousePos.x, y - mousePos.y );
	}
	mousePos.x = x, mousePos.y = y;
}

void Renderer::MouseWheel( float y )
{
	if ( !ImGui::IsWindowHovered() )
	{
		camera.Zoom( -y );
	}
}

void Renderer::MouseDown( int button )
{
	if ( button == GLFW_MOUSE_BUTTON_1 && imgui.focus_mode )
	{
		if ( !ImGui::IsWindowHovered() )
		{
			FocusCamera( mousePos.x, mousePos.y );
			camera.moved = true;
		}
	}
}

void Renderer::KeyInput( std::map<int, int> keyMap )
{
	if ( keyMap[GLFW_KEY_W] == GLFW_PRESS || keyMap[GLFW_KEY_W] == GLFW_REPEAT ) camera.Move( CamDir::Forward, deltaTime );
	if ( keyMap[GLFW_KEY_A] == GLFW_PRESS || keyMap[GLFW_KEY_A] == GLFW_REPEAT ) camera.Move( CamDir::Left, deltaTime );
	if ( keyMap[GLFW_KEY_S] == GLFW_PRESS || keyMap[GLFW_KEY_S] == GLFW_REPEAT ) camera.Move( CamDir::Backwards, deltaTime );
	if ( keyMap[GLFW_KEY_D] == GLFW_PRESS || keyMap[GLFW_KEY_D] == GLFW_REPEAT ) camera.Move( CamDir::Right, deltaTime );
	if ( keyMap[GLFW_KEY_Q] == GLFW_PRESS || keyMap[GLFW_KEY_Q] == GLFW_REPEAT ) camera.Move( CamDir::Up, deltaTime );
	if ( keyMap[GLFW_KEY_E] == GLFW_PRESS || keyMap[GLFW_KEY_E] == GLFW_REPEAT ) camera.Move( CamDir::Down, deltaTime );
}

void Renderer::Gui()
{
	if ( ImGui::CollapsingHeader( "General" ) )
	{
		if ( ImGui::Checkbox( "Performance", &(imgui.print_performance) ) );
	}
	if ( ImGui::CollapsingHeader( "Camera" ) )
	{
		ImGui::SliderFloat( "Speed", &camera.speed, .01f, 5, "%.2f" );
		if ( ImGui::SliderFloat( "FOV", &camera.cam.fov, 30, 180, "%.2f" ) ) camera.moved = true;
		if ( ImGui::SliderFloat( "Aperture", &camera.cam.aperture, 0, .2f, "%.2f" ) ) camera.moved = true;
		if ( ImGui::SliderFloat( "Focal Length", &camera.cam.focalLength, .1f, 50, "%.2f" ) ) camera.moved = true;
		if ( ImGui::RadioButton( "Projection", &camera.cam.type, PROJECTION ) ) camera.moved = true;
		if ( ImGui::RadioButton( "FishEye", &camera.cam.type, FISHEYE ) ) camera.moved = true;
		ImGui::Checkbox( "Focus Mode", &(imgui.focus_mode) );
	}
	if ( ImGui::CollapsingHeader( "Render", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		if ( ImGui::Checkbox( "Anti-Aliasing", (bool*)(&(settings->antiAliasing)) ) ) camera.moved = true;
		ImGui::Checkbox( "Reset every frame", &(imgui.reset_every_frame) );
		if ( ImGui::TreeNodeEx( "Recompile options", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			ImGui::Checkbox( "Russian Roulette", &(imgui.dummy_russian_roulette) );
			ImGui::Checkbox( "Filter fireflies (don't use with kajiya)", &(imgui.filter_fireflies) );
			if ( ImGui::TreeNodeEx( "Shading Type", ImGuiTreeNodeFlags_DefaultOpen ) ) {
				ImGui::RadioButton( "Kajiya", &( imgui.dummy_shading_type ), 0 );
				ImGui::RadioButton( "NEE", &( imgui.dummy_shading_type ), 1 );
				ImGui::TreePop( );
			}
			if ( ImGui::TreeNodeEx( "Sampling Type", ImGuiTreeNodeFlags_DefaultOpen ) ) {
				if ( ImGui::RadioButton( "Hemisphere", &( imgui.dummy_sampling_type ), 0 ) )
					imgui.sampling_type = SAMPLING_HEMISPHERE;
				if ( ImGui::RadioButton( "Cosine-weighted", &( imgui.dummy_sampling_type ), 1 ) )
					imgui.sampling_type = SAMPLING_COSINE;
				ImGui::TreePop( );
			}
			if ( ImGui::Button( "Recompile OpenCL" ) )
			{
				switch ( imgui.dummy_shading_type )
				{
				case 0: imgui.shading_type = SHADING_SIMPLE;
					break;
				case 1: imgui.shading_type = SHADING_NEE;
					break;
				};
				imgui.use_russian_roulette = imgui.dummy_russian_roulette;
				//switch (imgui.dummy_bvh_type)
				//{
				//case 0: imgui.bvh_type = USE_BVH2;
				//	break;
				//case 1: imgui.bvh_type = USE_BVH4;
				//	break;
				//};
				InitWavefrontKernels();
				camera.moved = true;
			}
			ImGui::TreePop();
		}
		ImGui::Checkbox( "Show Energy Levels", &(imgui.show_energy_levels) );
		if ( imgui.show_energy_levels ) ImGui::Text( "%f", imgui.energy_total );
		//if ( ImGui::RadioButton( "Kajiya", &( settings->tracerType ), KAJIYA ) ) camera.moved = true;
	}
	if ( ImGui::CollapsingHeader( "BVH" ) )
	{
		// ImGui::Text( "(S)BVH surface overlap constant" );
		// ImGui::SliderFloat( "Alpha", &(bvh2->alpha), 0, 1, "%.4f" );
		// if (ImGui::Button( "Rebuild BVH" ))
		// {
		// 	// rebuild bvh with new alpha value
		// 	/*bvh2->Build( true );
		// 	bvhNodeBuffer->CopyToDevice();
		// 	bvhIdxBuffer->CopyToDevice();*/
		// }
		// ImGui::Spacing();
		if ( ImGui::TreeNodeEx( "Statistics", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			if ( ImGui::Checkbox( "Visualize BVH traversal", (bool*)(&(settings->renderBVH)) ) ) camera.moved = true;
			ImGui::Text( "Building time: %.2fms", scene.bvh2->stat_build_time );
			ImGui::Text( "Primitive count: %i", scene.bvh2->stat_prim_count );
			ImGui::Text( "Node count: %i", scene.bvh2->stat_node_count );
			ImGui::Text( "Tree depth: %i", scene.bvh2->stat_depth );
			ImGui::Text( "Total SAH cost: %.5f", scene.bvh2->stat_sah_cost );
			ImGui::Text( "# of spatial splits: %i", scene.bvh2->stat_spatial_splits );
			ImGui::Text( "# of primitives clipped: %i", scene.bvh2->stat_prims_clipped );
			ImGui::TreePop();
		}
	}
	if ( ImGui::CollapsingHeader( "Post Processing" ) )
	{
		ImGui::SliderFloat( "Vignetting", &(imgui.vignet_strength), 0.0f, 1.0f, "%.2f" );
		ImGui::SliderFloat( "Gamma Correction", &(imgui.gamma_strength), 0.0f, 2.0f, "%.2f" );
		ImGui::SliderFloat( "Chromatic Abberation", &(imgui.chromatic_strength), 0.0f, 1.0f, "%.2f" );
	}
	if ( ImGui::CollapsingHeader( "Screenshotting" ) )
	{
		static char str0[128] = "";
		ImGui::InputTextWithHint( "Screenshot", "filename", str0, IM_ARRAYSIZE( str0 ) );
		if ( ImGui::Button( "Save Image" ) )
		{
			std::string input( str0 );
			SaveFrame( ("screenshots/" + input + ".png").c_str() );
		}
	}

	//ImGui::ShowDemoWindow();
}
