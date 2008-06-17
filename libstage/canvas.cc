/** canvas.cc
  Implement the main world viewing area in FLTK and OpenGL. 
Author: Richard Vaughan (vaughan@sfu.ca)
$Id: canvas.cc,v 1.12 2008-03-03 07:01:12 rtv Exp $
 */

#include "stage_internal.hh"
#include "texture_manager.hh"
#include "replace.h"
#include <string>

using namespace Stg;

void StgCanvas::TimerCallback( StgCanvas* c )
{
  c->redraw();
  
  Fl::repeat_timeout(((double)c->interval/1000),
							 (Fl_Timeout_Handler)StgCanvas::TimerCallback, 
							 c);
}


	StgCanvas::StgCanvas( StgWorldGui* world, int x, int y, int w, int h) 
: Fl_Gl_Window(x,y,w,h)
{
	end();
	//show(); // must do this so that the GL context is created before configuring GL
	// but that line causes a segfault in Linux/X11! TODO: test in OS X

	this->world = world;
	selected_models = NULL;
	last_selection = NULL;

	use_perspective_camera = false;
	perspective_camera.setPose( -3.0, 0.0, 1.0 );
	perspective_camera.setPitch( 70.0 ); //look down
	
	startx = starty = 0;
	//panx = pany = stheta = sphi = 0.0;
	//scale = 15.0;
	interval = 50; //msec between redraws

	graphics = true;
	dragging = false;
	rotating = false;

	showflags = STG_SHOW_CLOCK | STG_SHOW_BLOCKS | STG_SHOW_GRID | STG_SHOW_DATA;

	// // start the timer that causes regular redraws
 	Fl::add_timeout( ((double)interval/1000), 
						  (Fl_Timeout_Handler)StgCanvas::TimerCallback, 
						  this);
}

StgCanvas::~StgCanvas()
{
}

void StgCanvas::InvertView( uint32_t invertflags )
{
	showflags = (showflags ^ invertflags);

	//   printf( "flags %u data %d grid %d blocks %d follow %d clock %d tree %d occ %d\n",
	// 	  showflags, 
	// 	  showflags & STG_SHOW_DATA,
	// 	  showflags & STG_SHOW_GRID,
	// 	  showflags & STG_SHOW_BLOCKS,
	// 	  showflags & STG_SHOW_FOLLOW,
	// 	  showflags & STG_SHOW_CLOCK,
	// 	  showflags & STG_SHOW_QUADTREE,
	// 	  showflags & STG_SHOW_OCCUPANCY );
}

StgModel* StgCanvas::Select( int x, int y )
{
  // TODO XX
  //return NULL;

	// render all models in a unique color
	make_current(); // make sure the GL context is current
	glClearColor ( 1,1,1,1 ); // white
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	glDisable(GL_DITHER);
	glDisable(GL_BLEND); // turns off alpha blending, so we read back
								// exactly what we write to a pixel

	// render all top-level, draggable models in a color that is their
	// id 
	for( GList* it= world->StgWorld::children; it; it=it->next )
	{
		StgModel* mod = (StgModel*)it->data;
		
		if( mod->GuiMask() & (STG_MOVE_TRANS | STG_MOVE_ROT ))
		{
			uint8_t rByte, gByte, bByte, aByte;
			uint32_t modelId = mod->id;
			rByte = modelId;
			gByte = modelId >> 8;
			bByte = modelId >> 16;
			aByte = modelId >> 24;

			//printf("mod->Id(): 0x%X, rByte: 0x%X, gByte: 0x%X, bByte: 0x%X, aByte: 0x%X\n", modelId, rByte, gByte, bByte, aByte);
			
			glColor4ub( rByte, gByte, bByte, aByte );
			mod->DrawPicker();			
		}
	}

	// read the color of the pixel in the back buffer under the mouse
	// pointer
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT,viewport);

	uint8_t rByte, gByte, bByte, aByte;
	uint32_t modelId;
	
	glReadPixels( x,viewport[3]-y,1,1,
				 GL_RED,GL_UNSIGNED_BYTE,(void*)&rByte );
	glReadPixels( x,viewport[3]-y,1,1,
				 GL_GREEN,GL_UNSIGNED_BYTE,(void*)&gByte );
	glReadPixels( x,viewport[3]-y,1,1,
				 GL_BLUE,GL_UNSIGNED_BYTE,(void*)&bByte );
	glReadPixels( x,viewport[3]-y,1,1,
				 GL_ALPHA,GL_UNSIGNED_BYTE,(void*)&aByte );
	
	modelId = rByte;
	modelId |= gByte << 8;
	modelId |= bByte << 16;
	modelId |= aByte << 24;
	
	//printf("Clicked rByte: 0x%X, gByte: 0x%X, bByte: 0x%X, aByte: 0x%X\n", rByte, gByte, bByte, aByte);
	//printf("-->model Id = 0x%X\n", modelId);
	
	StgModel* mod = StgModel::LookupId( modelId );

	//printf("%p %s %d %x\n", mod, mod ? mod->Token() : "(none)", id, id );

	// put things back the way we found them
	glEnable(GL_DITHER);
	glEnable(GL_BLEND);
	glClearColor ( 0.7, 0.7, 0.8, 1.0);

	if( mod ) // we clicked on a root model
	{
		// if it's already selected
		if( GList* link = g_list_find( selected_models, mod ) )
		{
			// remove it from the selected list
			selected_models = 
				g_list_remove_link( selected_models, link );

			mod->Disable();
		}			  
		else      
		{
			last_selection = mod;
			selected_models = g_list_prepend( selected_models, mod );
			mod->Enable();
		}

		invalidate();
	}

	return mod;
}

// convert from 2d window pixel to 3d world coordinates
void StgCanvas::CanvasToWorld( int px, int py, 
		double *wx, double *wy, double* wz )
{
	int viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	GLdouble modelview[16];
	glGetDoublev(GL_MODELVIEW_MATRIX, modelview);

	GLdouble projection[16];	
	glGetDoublev(GL_PROJECTION_MATRIX, projection);	

	GLfloat pz;
	glReadPixels( px, h()-py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &pz );
	gluUnProject( px, w()-py, pz, modelview, projection, viewport, wx,wy,wz );
}

int StgCanvas::handle(int event) 
{

	switch(event) 
	{
		case FL_MOUSEWHEEL:
			if( selected_models )
			{
				// rotate all selected models
				for( GList* it = selected_models; it; it=it->next )
				{
					StgModel* mod = (StgModel*)it->data;
					mod->AddToPose( 0,0,0, 0.1*(double)Fl::event_dy() );
				}
				redraw();
			}
			else
			{
				if( use_perspective_camera == true ) {
					perspective_camera.scroll( Fl::event_dy() / 10.0 );
				} else {
					camera.scale( Fl::event_dy(),  Fl::event_x(), w(), Fl::event_y(), h() );
				}
				invalidate();
				redraw();
			}
			return 1;

		case FL_MOVE: // moused moved while no button was pressed
			if( Fl::event_state( FL_CTRL ) )
			{          
				int dx = Fl::event_x() - startx;
				int dy = Fl::event_y() - starty;

				if( use_perspective_camera == true ) {
					perspective_camera.addYaw( -dx );
					perspective_camera.addPitch( -dy );
				} else {
					camera.pitch( 0.5 * static_cast<double>( dy ) );
					camera.yaw( 0.5 * static_cast<double>( dx ) );
				}

				invalidate();
				redraw();
			}
			else if( Fl::event_state( FL_ALT ) )
			{   
				int dx = Fl::event_x() - startx;
				int dy = Fl::event_y() - starty;

				if( use_perspective_camera == true ) {
					perspective_camera.move( -dx, dy, 0.0 );
				} else {
					camera.move( -dx, dy );

				}
				invalidate();

			}

			startx = Fl::event_x();
			starty = Fl::event_y();
			
			return 1;

		case FL_PUSH: // button pressed
			switch( Fl::event_button() )
			{
				case 1:
					startx = Fl::event_x();
					starty = Fl::event_y();
					if( Select( startx, starty ) )
						dragging = true;	  
					return 1;
				case 3:
					{
						puts( "button 3" );
						startx = Fl::event_x();
						starty = Fl::event_y();
						if( Select( startx, starty ) )
						{
							printf( "rotating" );
							rotating = true;	  
						}	    
						return 1;
					}
				default:
					return 0;
			}    

		case FL_DRAG: // mouse moved while button was pressed
			{
				int dx = Fl::event_x() - startx;
				int dy = Fl::event_y() - starty;

				switch( Fl::event_button() )
				{
					case 1:
						if( dragging )
						{
							assert(selected_models);

							double sx,sy,sz;
							CanvasToWorld( startx, starty,
									&sx, &sy, &sz );
							double x,y,z;
							CanvasToWorld( Fl::event_x(), Fl::event_y(),
									&x, &y, &z );

							// move all selected models to the mouse pointer
							for( GList* it = selected_models; it; it=it->next )
							{
								StgModel* mod = (StgModel*)it->data;
								mod->AddToPose( x-sx, y-sy, 0, 0 );
							}
						}
						else
						{
							camera.move( -dx, dy );
							invalidate(); // so the projection gets updated
						}
						break;	    
					case 3: // right button
						if( rotating )
						{
							// move all selected models to the mouse pointer
							for( GList* it = selected_models; it; it=it->next )
							{
								StgModel* mod = (StgModel*)it->data;
								mod->AddToPose( 0,0,0, 0.05*dx );
							}
						}
						break;
				}
			}
			startx = Fl::event_x();
			starty = Fl::event_y();

			redraw();
			return 1; // end case FL_DRAG

		case FL_RELEASE:   // mouse button released
			// unselect everyone unless shift is pressed
			if( ! Fl::event_state( FL_SHIFT ) )
			{
				for( GList* it=selected_models; it; it=it->next )
					((StgModel*)it->data)->Enable();

				g_list_free( selected_models );
				selected_models = NULL;
				dragging = false;
				rotating = false;
				redraw();
			}
			return 1;

		case FL_FOCUS :
		case FL_UNFOCUS :
			//.... Return 1 if you want keyboard events, 0 otherwise
			return 1;
		case FL_KEYBOARD:
			switch( Fl::event_key() )
			{
				case 'p': // pause
					world->TogglePause();
					break;
				case ' ': // space bar
					camera.resetAngle();
					//invalidate();
					redraw();
					break;
				case FL_Left:  camera.move( -10, 0 ); break;
				case FL_Right: camera.move( 10, 0 );; break;
				case FL_Down:    camera.move( 0, -10 );; break;
				case FL_Up:  camera.move( 0, 10 );; break;
				default:
							 return 0; // keypress unhandled
			}
			invalidate(); // update projection
			return 1;
			//case FL_SHORTCUT:
			///... shortcut, key is in Fl::event_key(), ascii in Fl::event_text()
			// ... Return 1 if you understand/use the shortcut event, 0 otherwise...
			//return 1;
		default:
			// pass other events to the base class...
			//printf( "EVENT %d\n", event );
			return Fl_Gl_Window::handle(event);
	}
}

void StgCanvas::FixViewport(int W,int H) 
{
	glLoadIdentity();
	glViewport(0,0,W,H);
}

void StgCanvas::DrawGlobalGrid()
{
	PushColor( 0,0,0,0.2 );
	gl_draw_grid( world->GetExtent() );
	PopColor();
}

void StgCanvas::renderFrame( bool robot_camera )
{	
	uint32_t showflags = this->showflags;
	if( robot_camera == true )
		showflags = STG_SHOW_BLOCKS;
	
	if( ! (showflags & STG_SHOW_TRAILS) || robot_camera == true )
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	glPushMatrix();

	// draw the world size rectangle in white, using the polygon offset
	// so it doesn't z-fight with the models
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(2.0, 2.0);

	glScalef( 1.0/world->Resolution(), 1.0/world->Resolution(), 0 );
	((StgWorldGui*)world)->DrawFloor();

	glDisable(GL_POLYGON_OFFSET_FILL);

	if( (showflags & STG_SHOW_QUADTREE) || (showflags & STG_SHOW_OCCUPANCY) && robot_camera == false )
	{
		glDisable( GL_LINE_SMOOTH );
		glLineWidth( 1 );
		glPolygonMode( GL_FRONT, GL_LINE );
		colorstack.Push(1,0,0);

		if( showflags & STG_SHOW_OCCUPANCY )
			((StgWorldGui*)world)->DrawTree( false );

		if( showflags & STG_SHOW_QUADTREE )
			((StgWorldGui*)world)->DrawTree( true );

		colorstack.Pop();

		glEnable( GL_LINE_SMOOTH );
	}

	glPopMatrix();

	if( showflags & STG_SHOW_GRID && robot_camera == false )
		DrawGlobalGrid();

	for( GList* it=selected_models; it; it=it->next )
		((StgModel*)it->data)->DrawSelected();

	// draw the models
	if( showflags ) // if any bits are set there's something to draw
	{
		if( showflags & STG_SHOW_FOOTPRINT )
		{
			glDisable( GL_DEPTH_TEST );
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL );

			for( GList* it=world->StgWorld::children; it; it=it->next )
			{
				((StgModel*)it->data)->DrawTrailFootprint();
			}
			glEnable( GL_DEPTH_TEST );
		}

		if( showflags & STG_SHOW_TRAILRISE )
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL );

			for( GList* it=world->StgWorld::children; it; it=it->next )
			{
				((StgModel*)it->data)->DrawTrailBlocks();
			}
		}

		if( showflags & STG_SHOW_ARROWS )
		{
			glEnable( GL_DEPTH_TEST );
			for( GList* it=world->StgWorld::children; it; it=it->next )
			{
				((StgModel*)it->data)->DrawTrailArrows();
			}
		}

		if( showflags & STG_SHOW_BLOCKS )
		{
			for( GList* it=world->StgWorld::children; it; it=it->next )
			{
				StgModel* mod = ((StgModel*)it->data);

				if( mod->displaylist == 0 )
				  mod->displaylist = glGenLists(1);

				if( mod->rebuild_displaylist )
				  {
					 //printf( "Model %s is dirty\n", mod->Token() );					 
					 mod->BuildDisplayList( showflags ); // ready to be rendered
				  }

				// move into this model's local coordinate frame
				glPushMatrix();
				gl_pose_shift( &mod->pose );
				gl_pose_shift( &mod->geom.pose );

				// render the pre-recorded graphics for this model and
				// its children
				glCallList( mod->displaylist );

				glPopMatrix();
			}
		}

		//mod->Draw( showflags ); // draw the stuff that changes every update
		// draw everything else
		for( GList* it=world->StgWorld::children; it; it=it->next )
			((StgModel*)it->data)->Draw( showflags, this );
	}

	if( world->GetRayList() )
	{
		glDisable( GL_DEPTH_TEST );
		PushColor( 0,0,0,0.5 );
		for( GList* it = world->GetRayList(); it; it=it->next )
		{
			float* pts = (float*)it->data;
			glBegin( GL_LINES );
			glVertex2f( pts[0], pts[1] );
			glVertex2f( pts[2], pts[3] );
			glEnd();
		}  
		PopColor();
		glEnable( GL_DEPTH_TEST );

		world->ClearRays();
	}   

	if( showflags & STG_SHOW_CLOCK )
	{
		//use orthogonal projeciton without any zoom
		glMatrixMode (GL_PROJECTION);
		glPushMatrix(); //save old projection
		glLoadIdentity ();
		glOrtho( -w()/2.0, w()/2.0, -h()/2.0, h()/2.0, -100, 100 );	
		glMatrixMode (GL_MODELVIEW);

		glPushMatrix();
		glLoadIdentity();
		glDisable( GL_DEPTH_TEST );

		// if trails are on, we need to clear the clock background

		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

		colorstack.Push( 0.8,0.8,1.0 ); // pale blue
		glRectf( -w()/2, -h()/2, -w()/2 +120, -h()/2+20 );
		colorstack.Pop();

		char clockstr[50];
		world->ClockString( clockstr, 50 );

		colorstack.Push( 0,0,0 ); // black
		gl_draw_string( -w()/2+4, -h()/2+4, 5, clockstr );
		colorstack.Pop();

		glEnable( GL_DEPTH_TEST );
		glPopMatrix();

		//restore camera projection
		glMatrixMode (GL_PROJECTION);
		glPopMatrix();
		glMatrixMode (GL_MODELVIEW);
	}

}

void StgCanvas::draw()
{
	static bool loaded_texture = false;

	//Enable the following to debug camera model
	if( loaded_texture == true && use_perspective_camera == true )
		return;

	if (!valid() ) 
	{ 
		valid(1);
		FixViewport(w(), h());

		// set gl state that won't change every redraw
		glClearColor ( 0.7, 0.7, 0.8, 1.0);
		//glClearColor ( 1,1,1,1 );
		glDisable(GL_LIGHTING);
		glEnable (GL_DEPTH_TEST);
		glDepthFunc (GL_LESS);
		glCullFace( GL_BACK );
		glEnable (GL_CULL_FACE);
		glEnable( GL_BLEND );
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		glEnable( GL_LINE_SMOOTH );
		glHint (GL_LINE_SMOOTH_HINT, GL_FASTEST);
		glDepthMask(GL_TRUE);
		glEnable( GL_TEXTURE_2D );


		//TODO find a better home for loading textures
		if( loaded_texture == false ) {
			std::string fullpath;
			fullpath = world->fileMan.fullPath( "stall.png" );
			if ( fullpath == "" ) {
				PRINT_DEBUG( "Unable to load texture.\n" );
			}
			
			GLuint stall_id = TextureManager::getInstance().loadTexture( fullpath.c_str() );
			
			TextureManager::getInstance()._stall_texture_id = stall_id;

			loaded_texture = true;
		}

		// install a font
		gl_font( FL_HELVETICA, 12 );

		if( use_perspective_camera == true ) {
			perspective_camera.setAspect( static_cast< float >( w() ) / static_cast< float >( h() ) );
			perspective_camera.SetProjection();
		} else {
			stg_bounds3d_t extent = world->GetExtent();
			camera.SetProjection( w(), h(), extent.y.min, extent.y.max );
			camera.Draw();
		}

		// enable vertex arrays
		glEnableClientState( GL_VERTEX_ARRAY );
		//glEnableClientState( GL_COLOR_ARRAY );

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}            

	
	if( use_perspective_camera == true ) {
		if( (showflags & STG_SHOW_FOLLOW)  && last_selection ) {
			//Follow the selected robot
			stg_pose_t gpose = last_selection->GetGlobalPose();
			perspective_camera.setPose( gpose.x, gpose.y, 0.1 );
			perspective_camera.setYaw( rtod( gpose.a ) - 90.0 );
			
		}
		perspective_camera.Draw();
	} else if( (showflags & STG_SHOW_FOLLOW)  && last_selection ) {
		//Follow the selected robot
		stg_pose_t gpose = last_selection->GetGlobalPose();
		camera.setPose( gpose.x, gpose.y );
		
		stg_bounds3d_t extent = world->GetExtent();
		camera.SetProjection( w(), h(), extent.y.min, extent.y.max );
		camera.Draw();
	}
	
	renderFrame();
}

void StgCanvas::resize(int X,int Y,int W,int H) 
{
	Fl_Gl_Window::resize(X,Y,W,H);
	FixViewport(W,H);
	invalidate();
}

