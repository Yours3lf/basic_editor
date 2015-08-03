#include "framework.h"
#include "intersection.h"

#include "debug_draw.h"

using namespace prototyper;

DebugDrawManager ddman;

// TODO:
// ---proper multi-selection handling (still separate transformation basis)
// group/ungroup (common transformation basis), copy, paste, cut
// duplicate (chained events, do it packed?)
// ---do we really need set_end? how to do it in a more robust way? --> yes. no I didn't find any. (we're not using it too often anyway)
//
// Editor controls: (--- means done)
// ---forward: w
// ---backward: s
// ---left: a
// ---right: d
// ---up: q
// ---down: e
// ---translate: t + drag mouse (camera relative)
// ---rotate: r + drag mouse (camera relative)
// ---scale: y + drag mouse (uniform scale, mouse drag: up / down)
// ---translate global: ctrl + t + drag mouse
// ---rotate global: ctrl + r + drag mouse
// ---copy object: ctrl + c
// ---paste object: ctrl + v (should paste at the mouse cursor)
// ---cut object: ctrl + x
// ---add object: space (should add it at the mouse cursor)
// ---delete object: del
// duplicate object: ctrl + space (begins translating it too)
// ---undo: ctrl + z
// ---redo: ctrl + shift + z
// ---select single object: left click
// ---add object to selection: shift + left click
// box-select: b + drag mouse (construct sub-frustum?)
// ---select all: ctrl + a
// ---deselect all: left click on non-object area
// ---invert selection: ctrl + i
// apply discrete amount of transformation: ctrl + shift + ...
// ---toggle wireframe: f
// group: ctrl + g
// ungroup: ctrl + shift + g
// ---toggle lock trasnformation to x / y / z planes: 1 / 2 / 3

class selection_object
{
  public:
    mat4 rotation_mat;
    vec3 translate_vec, scale_vec;
    bool selected;

    selection_object() :
      rotation_mat( mat4::identity ),
      translate_vec( vec3(0) ),
      scale_vec( vec3( 1 ) ),
      selected( false ) {}
};

vector<selection_object*> objects;
vector<selection_object*> selection_buffer;

class command
{
  public:
    selection_object* o;
    bool chained;
    enum command_type { PACKED, ADD, REMOVE, SELECT, DESELECT, GROUP, UNGROUP, TRANSLATE, ROTATE, SCALE, NONE } type;

    virtual void execute() = 0;
    virtual void unexecute() = 0;
    virtual void set_end( selection_object* e, command_type t ) = 0;

    command( selection_object* oo = 0, command_type ct = NONE ) : o( oo ), chained( false ), type( ct )
    {
    }
};

class history
{
    vector<command*> commandlist;
    int ptr; // -1 means empty

  public:

    void undo()
    {
      if( commandlist.size() > 0 && ptr < commandlist.size() && ptr > -1 )
        commandlist[ptr]->unexecute();

      if( ptr > -1 )
        --ptr;
    }

    void redo()
    {
      if( ptr < ( int )commandlist.size() - 1 )
        ++ptr;
      else return; //nothing to redo

      if( commandlist.size() > 0 && ptr < commandlist.size() && ptr > -1 )
        commandlist[ptr]->execute();
    }

    void put( command* c )
    {
      if( ptr > -1 )
        for( int d = ptr + 1; d < commandlist.size(); ++d )
          delete commandlist[d];

      ++ptr;
      commandlist.resize( ptr + 1 );
      commandlist[ptr] = c;
      commandlist[ptr]->execute();
    }

    void set_end( selection_object* o, command::command_type ct )
    {
      if( commandlist.size() > 0 )
        for( int c = commandlist.size() - 1; c > -1; --c )
          if( commandlist[c]->type == command::PACKED || ( commandlist[c]->o == o && commandlist[c]->type == ct ) )
          {
            commandlist[c]->set_end( o, ct );
            break;
          }
    }

    history() : ptr( -1 ) {}

    ~history()
    {
      for( int c = 0; c < commandlist.size(); ++c )
        delete commandlist[c];
    }
};

class packed_command : public command
{
    vector< command* > pack;
  public:
    void execute()
    {
    for( auto & c : pack )
      {
        c->execute();
      }
    }

    void unexecute()
    {
    for( auto & c : pack )
      {
        c->unexecute();
      }
    }

    void put( command* c )
    {
      pack.push_back( c );
    }

    void set_end( selection_object* e, command_type t )
    {
    for( auto & c : pack )
      {
        if( c->type == t && c->o == e )
        {
          c->set_end( e, t );
        }
      }
    }

    bool empty()
    {
      return pack.empty();
    }

    packed_command( selection_object* oo = 0, command_type ct = PACKED ) : command( oo, ct )
    {
    }

    ~packed_command()
    {
    for( auto & c : pack )
      {
        delete c;
      }
    }
};

class add_command : public command
{
  public:
    void execute()
    {
      objects.push_back( o );
    }

    void unexecute()
    {
      for( auto c = objects.begin(); c != objects.end(); ++c )
        if( *c == o )
        {
          objects.erase( c );
          break;
        }
    }

    void set_end( selection_object* e, command_type t )
    {
    }

    ~add_command()
    {
      delete o;
    }

    add_command( selection_object* oo, command_type ct = ADD ) : command( oo, ct )
    {
    }
};

class remove_command : public command
{
  public:
    void execute()
    {
      for( auto c = objects.begin(); c != objects.end(); ++c )
        if( *c == o )
        {
          objects.erase( c );
          break;
        }
    }

    void unexecute()
    {
      objects.push_back( o );
    }

    void set_end( selection_object* e, command_type t )
    {
    }

    ~remove_command()
    {
      delete o;
    }

    remove_command( selection_object* oo, command_type ct = REMOVE ) : command( oo, ct )
    {
    }
};

class select_command : public command
{
  public:
    void execute()
    {
      o->selected = true;
    }

    void unexecute()
    {
      o->selected = false;
    }

    void set_end( selection_object* e, command_type t )
    {
    }

    select_command( selection_object* oo, command_type ct = SELECT ) : command( oo, ct )
    {
    }
};

class deselect_command : public command
{
  public:
    void execute()
    {
      o->selected = false;
    }

    void unexecute()
    {
      o->selected = true;
    }

    void set_end( selection_object* e, command_type t )
    {
    }

    deselect_command( selection_object* oo, command_type ct = DESELECT ) : command( oo, ct )
    {
    }
};

//group, ungroup

class translate_command : public command
{
  public:
    vec3 startstate, endstate;

    void execute()
    {
      o->translate_vec = endstate;
    }

    void unexecute()
    {
      o->translate_vec = startstate;
    }

    void set_end( selection_object* e, command_type t )
    {
      endstate = e->translate_vec;
    }

    translate_command( selection_object* oo, const vec3& s, const vec3& e, command_type ct = TRANSLATE ) : command( oo, ct ), startstate( s ), endstate( e )
    {
    }
};

class rotate_command : public command
{
  public:
    mat4 startstate, endstate;

    void execute()
    {
      o->rotation_mat = endstate;
    }

    void unexecute()
    {
      o->rotation_mat = startstate;
    }

    void set_end( selection_object* e, command_type t )
    {
      endstate = e->rotation_mat;
    }

    rotate_command( selection_object* oo, const mat4& s, const mat4& e, command_type ct = ROTATE ) : command( oo, ct ), startstate( s ), endstate( e )
    {
    }
};

class scale_command : public command
{
  public:
    vec3 startstate, endstate;

    void execute()
    {
      o->scale_vec = endstate;
    }

    void unexecute()
    {
      o->scale_vec = startstate;
    }

    void set_end( selection_object* e, command_type t )
    {
      endstate = e->scale_vec;
    }

    scale_command( selection_object* oo, const vec3& s, const vec3& e, command_type ct = SCALE ) : command( oo, ct ), startstate( s ), endstate( e )
    {
    }
};

int main( int argc, char** argv )
{
  shape::set_up_intersection();

  map<string, string> args;

  for( int c = 1; c < argc; ++c )
  {
    args[argv[c]] = c + 1 < argc ? argv[c + 1] : "";
    ++c;
  }

  cout << "Arguments: " << endl;
  for_each( args.begin(), args.end(), []( pair<string, string> p )
  {
    cout << p.first << " " << p.second << endl;
  } );

  uvec2 screen( 0 );
  bool fullscreen = false;
  bool silent = false;
  string title = "Basic selection prototype";

  /*
     * Process program arguments
     */

  stringstream ss;
  ss.str( args["--screenx"] );
  ss >> screen.x;
  ss.clear();
  ss.str( args["--screeny"] );
  ss >> screen.y;
  ss.clear();

  if( screen.x == 0 )
  {
    screen.x = 1280;
  }

  if( screen.y == 0 )
  {
    screen.y = 720;
  }

  try
  {
    args.at( "--fullscreen" );
    fullscreen = true;
  }
  catch( ... ) {}

  try
  {
    args.at( "--help" );
    cout << title << ", written by Marton Tamas." << endl <<
         "Usage: --silent      //don't display FPS info in the terminal" << endl <<
         "       --screenx num //set screen width (default:1280)" << endl <<
         "       --screeny num //set screen height (default:720)" << endl <<
         "       --fullscreen  //set fullscreen, windowed by default" << endl <<
         "       --help        //display this information" << endl;
    return 0;
  }
  catch( ... ) {}

  try
  {
    args.at( "--silent" );
    silent = true;
  }
  catch( ... ) {}

  /*
     * Initialize the OpenGL context
     */

  framework frm;
  frm.init( screen, title, fullscreen );
  frm.set_vsync( true );

//set opengl settings
  glEnable( GL_DEPTH_TEST );
  glDepthFunc( GL_LEQUAL );
  glFrontFace( GL_CCW );
  glEnable( GL_CULL_FACE );
  glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
  glClearDepth( 1.0f );

  frm.get_opengl_error();

  /*
     * Set up mymath
     */

  camera<float> cam;
  frame<float> the_frame;

  float cam_fov = radians( 45.0f );
  float cam_near = 1.0f;
  float cam_far = 100.0f;
  float aspect = ( float )screen.x / ( float )screen.y;

  the_frame.set_perspective( cam_fov, aspect, cam_near, cam_far );

  glViewport( 0, 0, screen.x, screen.y );

  /*
     * Set up the scene
     */

  GLuint box = frm.create_box();

  vector<vec3> vertices;
  vertices.push_back( vec3( -1, -1, 1 ) );
  vertices.push_back( vec3( 1, -1, 1 ) );
  vertices.push_back( vec3( 1, 1, 1 ) );

  vertices.push_back( vec3( -1, -1, 1 ) );
  vertices.push_back( vec3( 1, 1, 1 ) );
  vertices.push_back( vec3( -1, 1, 1 ) );

//
  vertices.push_back( vec3( 1, 1, -1 ) );
  vertices.push_back( vec3( 1, -1, -1 ) );
  vertices.push_back( vec3( -1, -1, -1 ) );

  vertices.push_back( vec3( -1, 1, -1 ) );
  vertices.push_back( vec3( 1, 1, -1 ) );
  vertices.push_back( vec3( -1, -1, -1 ) );

//
  vertices.push_back( vec3( -1, 1, 1 ) );
  vertices.push_back( vec3( -1, -1, -1 ) );
  vertices.push_back( vec3( -1, -1, 1 ) );

  vertices.push_back( vec3( -1, 1, -1 ) );
  vertices.push_back( vec3( -1, -1, -1 ) );
  vertices.push_back( vec3( -1, 1, 1 ) );

//
  vertices.push_back( vec3( 1, -1, 1 ) );
  vertices.push_back( vec3( 1, -1, -1 ) );
  vertices.push_back( vec3( 1, 1, 1 ) );

  vertices.push_back( vec3( 1, 1, 1 ) );
  vertices.push_back( vec3( 1, -1, -1 ) );
  vertices.push_back( vec3( 1, 1, -1 ) );

//
  vertices.push_back( vec3( -1, 1, 1 ) );
  vertices.push_back( vec3( 1, 1, 1 ) );
  vertices.push_back( vec3( 1, 1, -1 ) );

  vertices.push_back( vec3( -1, 1, 1 ) );
  vertices.push_back( vec3( 1, 1, -1 ) );
  vertices.push_back( vec3( -1, 1, -1 ) );

//
  vertices.push_back( vec3( 1, -1, -1 ) );
  vertices.push_back( vec3( 1, -1, 1 ) );
  vertices.push_back( vec3( -1, -1, 1 ) );

  vertices.push_back( vec3( -1, -1, -1 ) );
  vertices.push_back( vec3( 1, -1, -1 ) );
  vertices.push_back( vec3( -1, -1, 1 ) );

  /*
     * Set up the shaders
     */

  GLuint sel_shader = 0;
  frm.load_shader( sel_shader, GL_VERTEX_SHADER, "../shaders/selection/selection.vs" );
  frm.load_shader( sel_shader, GL_FRAGMENT_SHADER, "../shaders/selection/selection.ps" );

  GLint sel_mvp_mat_loc = glGetUniformLocation( sel_shader, "mvp" );
  GLint sel_col_loc = glGetUniformLocation( sel_shader, "col" );

  /*
     * Handle events
     */

  vec2 mouse_pos = vec2(0);
  bool translate_begin = false, rotate_begin = false, scale_begin = false;
  bool translate_end = false, rotate_end = false, scale_end = false;
  bool translate_action = false, rotate_action = false, scale_action = false;
  bool warped = false, clicked = false;
  bool wireframe = false;
  bool lock_to_x = false, lock_to_y = false, lock_to_z = false;
  history his;

  cam.move_forward( -5 );

  bool cam_warped = false, cam_ignore = true, cam_rotate = false;
  vec3 movement_speed = vec3(0);
  float move_amount = 0.05;

  packed_command* pc = 0;

  auto event_handler = [&]( const sf::Event & ev )
  {
    switch( ev.type )
    {
      case sf::Event::KeyPressed:
        {
          if( ev.key.code == sf::Keyboard::T && !translate_action )
          {
            translate_begin = true;
          }

          if( ev.key.code == sf::Keyboard::R && !rotate_action )
          {
            rotate_begin = true;
          }

          if( ev.key.code == sf::Keyboard::Y && !scale_action )
          {
            scale_begin = true;
          }

          if( ev.key.code == sf::Keyboard::F )
          {
            wireframe = !wireframe;
          }

          if( ev.key.code == sf::Keyboard::Space )
          {
            //his.put( new add_command( new object() ) );
            pc->put( new add_command( new selection_object() ) );
          }

          if( ev.key.code == sf::Keyboard::Delete )
          {
            for( auto c = objects.begin(); c != objects.end(); ++c )
            {
              if( ( *c )->selected )
              {
                //his.put( new remove_command( *c ) );
                pc->put( new remove_command( *c ) );
              }
            }
          }

          if( ev.key.code == sf::Keyboard::Z )
          {
            if( sf::Keyboard::isKeyPressed( sf::Keyboard::LControl ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RControl ) )
            {
              if( sf::Keyboard::isKeyPressed( sf::Keyboard::LShift ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RShift ) )
                his.redo();
              else
                his.undo();
            }
          }

          if( ev.key.code == sf::Keyboard::C )
          {
            if( sf::Keyboard::isKeyPressed( sf::Keyboard::LControl ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RControl ) )
            {
            for( auto & c : selection_buffer )
              {
                delete c;
              }

              selection_buffer.clear();

            for( auto & c : objects )
              {
                if( c->selected )
                {
                  selection_buffer.push_back( new selection_object( *c ) );
                }
              }
            }
          }

          if( ev.key.code == sf::Keyboard::X )
          {
            if( sf::Keyboard::isKeyPressed( sf::Keyboard::LControl ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RControl ) )
            {
            for( auto & c : selection_buffer )
              {
                delete c;
              }

              selection_buffer.clear();

              for( auto c = objects.begin(); c != objects.end(); ++c )
              {
                if( ( *c )->selected )
                {
                  //his.put( new remove_command( *c ) );
                  pc->put( new remove_command( *c ) );

                  selection_buffer.push_back( new selection_object( **c ) );
                }
              }
            }
          }

          if( ev.key.code == sf::Keyboard::V )
          {
            if( sf::Keyboard::isKeyPressed( sf::Keyboard::LControl ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RControl ) )
            {
            for( auto & c : selection_buffer )
              {
              selection_object* o = new selection_object( *c );
                //his.put( new add_command( o ) );
                pc->put( new add_command( o ) );
                o->selected = false;
              }
            }
          }

          if( ev.key.code == sf::Keyboard::A )
          {
            if( sf::Keyboard::isKeyPressed( sf::Keyboard::LControl ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RControl ) )
            {
            for( auto & c : objects )
              {
                if( !c->selected )
                {
                  //his.put( new select_command( c ) );
                  pc->put( new select_command( c ) );
                }
              }
            }
          }

          if( ev.key.code == sf::Keyboard::I )
          {
            if( sf::Keyboard::isKeyPressed( sf::Keyboard::LControl ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RControl ) )
            {
            for( auto & c : objects )
              {
                if( !c->selected )
                {
                  //his.put( new select_command( c ) );
                  pc->put( new select_command( c ) );
                }
                else
                {
                  //his.put( new deselect_command( c ) );
                  pc->put( new deselect_command( c ) );
                }
              }
            }
          }

          if( ev.key.code == sf::Keyboard::Num1 )
          {
            lock_to_x = !lock_to_x;
            lock_to_y = false;
            lock_to_z = false;
          }

          if( ev.key.code == sf::Keyboard::Num2 )
          {
            lock_to_y = !lock_to_y;
            lock_to_x = false;
            lock_to_z = false;
          }

          if( ev.key.code == sf::Keyboard::Num3 )
          {
            lock_to_z = !lock_to_z;
            lock_to_y = false;
            lock_to_x = false;
          }

          break;
        }
      case sf::Event::KeyReleased:
        {
          if( ev.key.code == sf::Keyboard::T )
          {
            translate_end = true;
          }

          if( ev.key.code == sf::Keyboard::R )
          {
            rotate_end = true;
          }

          if( ev.key.code == sf::Keyboard::Y )
          {
            scale_end = true;
          }

          break;
        }
      case sf::Event::MouseMoved:
        {
          mouse_pos.x = ev.mouseMove.x / ( float )screen.x;
          mouse_pos.y = 1 - ev.mouseMove.y / ( float )screen.y;

          //handle fps like movement
          if( cam_rotate )
          {
            vec2 mpos( ev.mouseMove.x / float( screen.x ), ev.mouseMove.y / float( screen.y ) );

            if( cam_warped )
            {
              cam_ignore = false;
            }
            else
            {
              frm.set_mouse_pos( ivec2( screen.x / 2.0f, screen.y / 2.0f ) );
              cam_warped = true;
              cam_ignore = true;
            }

            if( !cam_ignore && all( notEqual( mpos, vec2( 0.5 ) ) ) )
            {
              cam.rotate( mm::radians( -180.0f * ( mpos.x - 0.5f ) ), mm::vec3( 0.0f, 1.0f, 0.0f ) );
              cam.rotate_x( mm::radians( -180.0f * ( mpos.y - 0.5f ) ) );
              frm.set_mouse_pos( ivec2( screen.x / 2.0f, screen.y / 2.0f ) );
              cam_warped = true;
            }
          }

          break;
        }
      case sf::Event::MouseButtonPressed:
        {
          if( ev.mouseButton.button == sf::Mouse::Left )
          {
            clicked = true;
          }

          if( ev.mouseButton.button == sf::Mouse::Right )
          {
            cam_rotate = true;
            cam_ignore = true;
            cam_warped = false;
          }

          break;
        }
      case sf::Event::MouseButtonReleased:
        {
          if( ev.mouseButton.button == sf::Mouse::Right )
          {
            cam_rotate = false;
          }

          break;
        }
      default:
        break;
    }
  };

  /*
     * Render
     */

  sf::Clock timer;
  timer.restart();

  frm.display( [&]
  {
    if( !pc )
      pc = new packed_command();

    packed_command* pcc = pc;

    frm.handle_events( event_handler );

    float seconds = timer.getElapsedTime().asMilliseconds() / 1000.0f;

    if( seconds > 0.016f ) // 16 ms
    {
      if( sf::Keyboard::isKeyPressed( sf::Keyboard::A ) )
      {
        movement_speed.x -= move_amount;
      }

      if( sf::Keyboard::isKeyPressed( sf::Keyboard::D ) )
      {
        movement_speed.x += move_amount;
      }

      if( sf::Keyboard::isKeyPressed( sf::Keyboard::W ) )
      {
        movement_speed.z += move_amount;
      }

      if( sf::Keyboard::isKeyPressed( sf::Keyboard::S ) )
      {
        movement_speed.z -= move_amount;
      }

      if( sf::Keyboard::isKeyPressed( sf::Keyboard::Q ) )
      {
        movement_speed.y += move_amount;
      }

      if( sf::Keyboard::isKeyPressed( sf::Keyboard::E ) )
      {
        movement_speed.y -= move_amount;
      }

      cam.move_right( movement_speed.x * seconds * 10 );
      cam.move_up( movement_speed.y * seconds * 10 );
      cam.move_forward( movement_speed.z * seconds * 10 );
      movement_speed *= 0.955;

      timer.restart();
    }

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glPolygonMode( GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL );

    glUseProgram( sel_shader );

    mat4 view = cam.get_matrix();

    if( clicked )
    {
      if( !( sf::Keyboard::isKeyPressed( sf::Keyboard::LShift ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RShift ) ) )
      {
      for( auto & d : objects )
        {
          if( d->selected )
          {
            //his.put( new deselect_command( d ) );
            pc->put( new deselect_command( d ) );
          }
        }
      }
    }

  for( auto& c : objects )
    {
    mat4 trans = create_translation( c->translate_vec ) * c->rotation_mat * create_scale( c->scale_vec );

    mat4 model = trans;
    mat4 projection = the_frame.projection_matrix;
    mat4 mv = view * model;
    mat4 mvp = projection * mv;

      mat4 inv_mvp = inverse( mvp );
      vec4 viewport = vec4( 0, 0, screen.x, screen.y );

      vec3 ori, dir;
      vec2 mouse_pos_ndc = mouse_pos * 2 - 1;

      vec3 ray_start = unproject( vec3(mouse_pos_ndc, 0), inv_mvp );
      vec3 ray_end = unproject( vec3(mouse_pos_ndc, 1), inv_mvp );
      ori = ray_start;
      dir = normalize(ray_end - ray_start);

      vec3 col( 1, 0, 0 );

      ray obj_space_ray(ori, dir);
        
      aabb obj_space_aabb;
      for( auto& d : vertices )
        obj_space_aabb.expand( d );

      aabb model_space_aabb;
      for( auto& d : vertices )
        model_space_aabb.expand( (model * vec4( d, 1 )).xyz );

      ddman.CreateAABoxMinMax( model_space_aabb.min, model_space_aabb.max, 0 );

      if( clicked )
      {
        ddman.CreateLineSegment( obj_space_ray.origin, obj_space_ray.direction * 10000, -1 );

        int result = 0;
        /**
        for( int d = 0; d < 12; ++d )
        {
          triangle t(vertices[d* 3 + 0], vertices[d* 3 + 1], vertices[d* 3 + 2]);
          if( r.intersects( &t ) )
          {
            result = 1;
            break;
          }
        }
        /**/
        
        /**/
        result = obj_space_aabb.is_intersecting( &obj_space_ray );
        /**/

        if( result )
        {
          //his.put( new select_command( c ) );
          pc->put( new select_command( c ) );
          clicked = false;
        }
      }

      if( c->selected )
      {
        col = vec3( 0, 1, 0 );

        if( translate_begin && !translate_action )
        {
          //his.put( new translate_command( c, c->translate_vec, c->translate_vec ) );
          pc->put( new translate_command( c, c->translate_vec, c->translate_vec ) );
        }

        if( rotate_begin && !rotate_action )
        {
          //his.put( new rotate_command( c, c->rotation_mat, c->rotation_mat ) );
          pc->put( new rotate_command( c, c->rotation_mat, c->rotation_mat ) );
        }

        if( scale_begin && !scale_action )
        {
          //his.put( new scale_command( c, c->scale_vec, c->scale_vec ) );
          pc->put( new scale_command( c, c->scale_vec, c->scale_vec ) );
        }

        if( translate_end )
        {
          translate_action = false;
        }

        if( rotate_end )
        {
          rotate_action = false;
        }

        if( scale_end )
        {
          scale_action = false;
        }

        if( translate_action && warped )
        {
          vec2 delta = mouse_pos - 0.5;
          float top = length( c->translate_vec - cam.pos ) * tan( cam_fov * 0.5f );
          float right = top * aspect;

          if( sf::Keyboard::isKeyPressed( sf::Keyboard::LControl ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RControl ) )
          {
            if( lock_to_x )
            {
              c->translate_vec += delta.x * vec3( 1, 0, 0 ) * 2 * top;
            }
            else if( lock_to_z )
            {
              c->translate_vec += -delta.y * vec3( 0, 0, 1 ) * 2 * right;
            }
            else
            {
              c->translate_vec += delta.x * vec3( 1, 0, 0 ) * 2 * top;
              c->translate_vec += -delta.y * vec3( 0, 0, 1 ) * 2 * right;
            }
          }
          else
          {
            vec3 right_vec = normalize( cross( cam.view_dir, cam.up_vector ) );
            vec3 up_vec = normalize( cam.up_vector );

            if( lock_to_x )
            {
              c->translate_vec += delta.x * right_vec * 2 * top;
            }
            else if( lock_to_y )
            {
              c->translate_vec += delta.y * up_vec * 2 * right;
            }
            else
            {
              c->translate_vec += delta.x * right_vec * 2 * top;
              c->translate_vec += delta.y * up_vec * 2 * right;
            }
          }
        }
        else if( rotate_action && warped )
        {
          vec2 delta = mouse_pos - 0.5;

          if( sf::Keyboard::isKeyPressed( sf::Keyboard::LControl ) || sf::Keyboard::isKeyPressed( sf::Keyboard::RControl ) )
          {
            if( lock_to_x )
            {
              c->rotation_mat = create_rotation( radians( -delta.y * 40 ), vec3( 1, 0, 0 ) ) * c->rotation_mat;
            }
            else if( lock_to_y )
            {
              c->rotation_mat = create_rotation( radians( delta.x * 40 ), vec3( 0, 1, 0 ) ) * c->rotation_mat;
            }
            else
            {
              c->rotation_mat = create_rotation( radians( -delta.y * 40 ), vec3( 1, 0, 0 ) ) * c->rotation_mat;
              c->rotation_mat = create_rotation( radians( delta.x * 40 ), vec3( 0, 1, 0 ) ) * c->rotation_mat;
            }
          }
          else
          {
            vec3 right_vec = normalize( cross( cam.view_dir, cam.up_vector ) );
            vec3 up_vec = normalize( cam.up_vector );

            if( lock_to_x )
            {
              c->rotation_mat = create_rotation( radians( -delta.y * 40 ), right_vec ) * c->rotation_mat;
            }
            else if( lock_to_y )
            {
              c->rotation_mat = create_rotation( radians( delta.x * 40 ), up_vec ) * c->rotation_mat;
            }
            else
            {
              c->rotation_mat = create_rotation( radians( -delta.y * 40 ), right_vec ) * c->rotation_mat;
              c->rotation_mat = create_rotation( radians( delta.x * 40 ), up_vec ) * c->rotation_mat;
            }
          }
        }
        else if( scale_action && warped )
        {
          vec2 delta = mouse_pos - 0.5;
          vec3 dir = vec3( delta.y > 0 ? 1 : -1 );
          c->scale_vec += length( delta ) * dir;
          c->scale_vec = max( c->scale_vec, vec3( 0.01 ) );
        }
      }

      glUniformMatrix4fv( sel_mvp_mat_loc, 1, false, &mvp[0].x );
      glUniform3fv( sel_col_loc, 1, &col.x );

      glBindVertexArray( box );
      glDrawElements( GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0 );
    }

    if( translate_action || rotate_action || scale_action )
    {
      frm.set_mouse_pos( ivec2( screen.x / 2, screen.y / 2 ) );
      warped = true;
    }
    else
    {
      warped = false;
    }

    if( !pc->empty() )
    {
      his.put( pc );
      pc = 0;
    }

    if( translate_begin )
    {
      translate_action = true;
      translate_begin = false;
    }

    if( rotate_begin )
    {
      rotate_action = true;
      rotate_begin = false;
    }

    if( scale_begin )
    {
      scale_action = true;
      scale_begin = false;
    }

    if( translate_end )
    {
    for( auto & c : objects )
        if( c->selected )
          his.set_end( c, command::TRANSLATE );
    }

    if( rotate_end )
    {
    for( auto & c : objects )
        if( c->selected )
          his.set_end( c, command::ROTATE );
    }

    if( scale_end )
    {
    for( auto & c : objects )
        if( c->selected )
          his.set_end( c, command::SCALE );
    }

    translate_end = false;
    rotate_end = false;
    scale_end = false;
    clicked = false;

    //draw reference grid
    glUseProgram( 0 );
    glPolygonMode( GL_FRONT_AND_BACK, GL_LINE ); //WIREFRAME
    glDisable( GL_TEXTURE_2D );

    glMatrixMode( GL_MODELVIEW );
    glLoadMatrixf( &view[0].x );

    glMatrixMode( GL_PROJECTION );
    glLoadMatrixf( &the_frame.projection_matrix[0].x );

    int size = 20;
    size /= 2;

    for( int x = -size; x < size + 1; x++ )
    {
      glBegin( GL_LINE_LOOP );
      glVertex3f( x, -2, -size );
      glVertex3f( x, -2, size );
      glEnd();
    };

    for( int y = -size; y < size + 1; y++ )
    {
      glBegin( GL_LINE_LOOP );
      glVertex3f( -size, -2, y );
      glVertex3f( size, -2, y );
      glEnd();
    };

    ddman.DrawAndUpdate(16);

    glEnable( GL_TEXTURE_2D );

    frm.get_opengl_error();
  }, silent );

  return 0;
}



