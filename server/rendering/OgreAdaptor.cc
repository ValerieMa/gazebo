/*
 *  Gazebo - Outdoor Multi-Robot Simulator
 *  Copyright (C) 2003  
 *     Nate Koenig & Andrew Howard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
/* Desc: Middleman between OGRE and Gazebo
 * Author: Nate Koenig
 * Date: 13 Feb 2006
 * CVS: $Id$
 */

#include <Ogre.h>
#include <OgreDataStream.h>
#include <OgreLogManager.h>
#include <OgreWindowEventUtilities.h>

#include <sys/types.h>
#include <dirent.h>

#include "MovableText.hh"
#include "InputEvent.hh"
#include "OgreHUD.hh"
#include "Entity.hh"
#include "GazeboError.hh"
#include "GazeboMessage.hh"
#include "Global.hh"
#include "GazeboError.hh"
#include "XMLConfig.hh"
#include "Gui.hh"
#include "OgreFrameListener.hh"
#include "OgreAdaptor.hh"

using namespace gazebo;

////////////////////////////////////////////////////////////////////////////////
/// Constructor
OgreAdaptor::OgreAdaptor()
{
  // Create a new log manager and prevent output from going to stdout
  this->logManager = new Ogre::LogManager();
  this->logManager->createLog("Ogre.log", true, false, false);

  // Make the root 
  this->root = new Ogre::Root(); 

  this->ogreWindow = true;
}

////////////////////////////////////////////////////////////////////////////////
/// Destructor
OgreAdaptor::~OgreAdaptor()
{
  if (this->window)
    delete this->window;

  if (this->backgroundColor)
    delete this->backgroundColor;

  if (this->frameListener)
    delete this->frameListener;
}

////////////////////////////////////////////////////////////////////////////////
// Init
void OgreAdaptor::Init(XMLConfigNode *node)
{
  XMLConfigNode *cnode;
  Ogre::ColourValue ambient;

  if (!node)
  {
    gzthrow( "missing OGRE Rendernig information" );
  }

  ambient.r = node->GetTupleDouble("ambient",0,0);
  ambient.g = node->GetTupleDouble("ambient",1,0);
  ambient.b = node->GetTupleDouble("ambient",2,0);
  ambient.a = node->GetTupleDouble("ambient",3,0);

  if ((cnode=node->GetChild("video")))
  {
    std::ostringstream stream;
    int width, height, depth;

    width = cnode->GetTupleInt("size",0,800);
    height = cnode->GetTupleInt("size",1,600);
    depth = cnode->GetInt("depth",16,0);

    stream << width << " x " << height << " @ " << depth << "-bit colour";
    this->videoMode = stream.str();
  }
  else
  {
    this->videoMode = "800 x 600 @ 16-bit colour";
  }

  // Default background color
  this->backgroundColor = new Ogre::ColourValue(Ogre::ColourValue::Black);


  // Load all the plugins
  this->LoadPlugins();

  // Setup the available resources 
  this->SetupResources();

  // Setup the rendering system, and create the context
  this->SetupRenderSystem(true);

  // Initialize the root node, and don't create a window
  this->window = this->root->initialise(false); 

  // Create a window for Ogre
  this->CreateWindow();

  // Get the SceneManager, in this case a generic one
  //this->sceneMgr = this->root->createSceneManager(Ogre::ST_GENERIC);
  this->sceneMgr = this->root->createSceneManager(Ogre::ST_EXTERIOR_CLOSE);

  // Set default mipmap level (NB some APIs ignore this)
  Ogre::TextureManager::getSingleton().setDefaultNumMipmaps( 5 );

  // Load Resources
  Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();

  // Get the SceneManager, in this case a generic one
  // Default lighting
  this->sceneMgr->setAmbientLight(ambient); 

  // Settings for shadow mapping
  //this->sceneMgr->setShadowTexturePixelFormat(Ogre::PF_FLOAT16_R);
  //this->sceneMgr->setShadowTechnique( Ogre::SHADOWTYPE_TEXTURE_ADDITIVE );
  this->sceneMgr->setShadowTechnique( Ogre::SHADOWTYPE_STENCIL_ADDITIVE );
  this->sceneMgr->setShadowTextureSelfShadow(true);

  // Add fog. This changes the background color
  if ((cnode = node->GetChild("fog")))
  {
    //Ogre::FogMode fogType = Ogre::FOG_NONE; 
    //std::string type;
    //double density;
    double linearStart, linearEnd;

    this->backgroundColor->r = cnode->GetTupleDouble("color",0,0);
    this->backgroundColor->g = cnode->GetTupleDouble("color",1,0);
    this->backgroundColor->b = cnode->GetTupleDouble("color",2,0);

    //type = cnode->GetString("type","linear",0);
    //density = cnode->GetDouble("density",0,0);
    linearStart = cnode->GetDouble("linearStart",0,0);
    linearEnd = cnode->GetDouble("linearEnd",1.0,0);

    /*if (type == "linear")
      fogType = Ogre::FOG_LINEAR;
    else if (type == "exp")
      fogType = Ogre::FOG_EXP;
    else if (type == "exp2")
      fogType = Ogre::FOG_EXP2;
      */

    //this->sceneMgr->setFog(fogType, *this->backgroundColor, density, linearStart, linearEnd);
    this->sceneMgr->setFog(Ogre::FOG_LINEAR, *this->backgroundColor, 0, linearStart, linearEnd);
  }
  else
  {
    this->sceneMgr->setFog(Ogre::FOG_LINEAR, *this->backgroundColor, 0, 1000, 1000);
  }

  // Add a sky dome to our scene
  if ((cnode = node->GetChild("sky")))
  {
    std::string material = cnode->GetString("material","",1);
    if (!material.empty())
    {
      try
      {
        if (node->GetChild("fog"))
        {
          Ogre::Plane plane;
          plane.d = 49;
          plane.normal = Ogre::Vector3::NEGATIVE_UNIT_Y;
          this->sceneMgr->setSkyPlane(true, plane, material, 50, 8, true, 0.5, 150, 150);
        }
        else
        {
          this->sceneMgr->setSkyDome(true,material,5,8);
        }

      }
      catch (int)
      {
        gzmsg(0) << "Unable to set sky dome to material[" << material << "]\n";
      }

    }
  }
  // Create our frame listener and register it
  this->frameListener = new OgreFrameListener();
  this->root->addFrameListener(this->frameListener);

  // Create the default camera. This camera is only used to view the output
  // of cameras created using the XML world file
  this->camera = this->sceneMgr->createCamera("__GAZEBO_CAMERA__");
  this->camera->setNearClipDistance(1);
  this->camera->setFarClipDistance(1);
  this->viewport = this->window->addViewport(this->camera);
  this->viewport->setBackgroundColour(Ogre::ColourValue::Black);

  this->camera->setAspectRatio( Ogre::Real(viewport->getActualWidth()) / Ogre::Real(viewport->getActualHeight()) );

  this->DrawGrid();
/*
  Ogre::ManualObject* myManualObject =  this->sceneMgr->createManualObject("manual1"); 
  Ogre::SceneNode* myManualObjectNode = this->sceneMgr->getRootSceneNode()->createChildSceneNode("manual1_node"); 

  Ogre::MaterialPtr myManualObjectMaterial = Ogre::MaterialManager::getSingleton().create("manual1Material","debugger"); 
  myManualObjectMaterial->setReceiveShadows(false); 
  myManualObjectMaterial->getTechnique(0)->setLightingEnabled(true); 
  myManualObjectMaterial->getTechnique(0)->getPass(0)->setDiffuse(0,0,1,0); 
  myManualObjectMaterial->getTechnique(0)->getPass(0)->setAmbient(0,0,1); 
  myManualObjectMaterial->getTechnique(0)->getPass(0)->setSelfIllumination(0,0,1); 

  myManualObject->begin("manual1Material", Ogre::RenderOperation::OT_LINE_LIST); 
  myManualObject->position(0, 0, 0); 
  myManualObject->position(0, 1, 0); 
  myManualObject->position(0, 0, 0); 
  myManualObject->position(0, 0, 1); 
  myManualObject->position(0, 0, 0); 
  myManualObject->position(1, 0, 0); 
  // etc 
  myManualObject->end(); 
  myManualObjectNode->attachObject(myManualObject);
  */
  
  //delete [] mstr;
}

////////////////////////////////////////////////////////////////////////////////
// Load plugins
void OgreAdaptor::LoadPlugins()
{
  std::string pathStr;
  std::string pluginStr;
  XMLConfigNode *pluginNode;
  std::list<std::string>::iterator iter;

  for (iter=Global::ogrePaths.begin(); iter!=Global::ogrePaths.end(); iter++)
  {
    DIR *dir;
    if ((dir=opendir((*iter).c_str())) == NULL)
    {
      continue;
    }
    closedir(dir);

    std::vector<std::string> plugins;
    std::vector<std::string>::iterator piter;

    plugins.push_back((*iter)+"/RenderSystem_GL.so");
    plugins.push_back((*iter)+"/Plugin_ParticleFX.so");
    plugins.push_back((*iter)+"/Plugin_BSPSceneManager.so");
    plugins.push_back((*iter)+"/Plugin_OctreeSceneManager.so");

    for (piter=plugins.begin(); piter!=plugins.end(); piter++)
    {
      try
      {
        // Load the plugin into OGRE
        this->root->loadPlugin(*piter);
      }
      catch (Ogre::Exception e)
      {
        gzthrow("Unable to load Ogre Plugins.\nMake sure the plugins path in the world file is set correctly");
      }
    }
  }

}

////////////////////////////////////////////////////////////////////////////////
// Setup resources
void OgreAdaptor::SetupResources()
{
  std::vector<std::string> archNames;
  std::vector<std::string>::iterator aiter;
  std::list<std::string>::iterator iter;

  for (iter=Global::gazeboPaths.begin(); 
       iter!=Global::gazeboPaths.end(); iter++)
  {


    DIR *dir;
    if ((dir=opendir((*iter).c_str())) == NULL)
    {
      continue;
    }
    closedir(dir);

    archNames.push_back((*iter)+"/Media");
    archNames.push_back((*iter)+"/Media/fonts");
    archNames.push_back((*iter)+"/Media/materials/programs");
    archNames.push_back((*iter)+"/Media/materials/scripts");
    archNames.push_back((*iter)+"/Media/materials/textures");
    archNames.push_back((*iter)+"/Media/models");

    for (aiter=archNames.begin(); aiter!=archNames.end(); aiter++)
    {
      try
      {
        Ogre::ResourceGroupManager::getSingleton().addResourceLocation( *aiter, "FileSystem", "General"); 
      }
      catch (Ogre::Exception)
      {
        gzthrow("Unable to load Ogre Resources.\nMake sure the resources path in the world file is set correctly.");
      }
    }
  }
}


////////////////////////////////////////////////////////////////////////////////
// Setup render system
void OgreAdaptor::SetupRenderSystem(bool create)
{
  // Set parameters of render system (window size, etc.)
  //if (!this->root->restoreConfig())
  {
    Ogre::RenderSystemList *rsList = this->root->getAvailableRenderers();
    int c = 0;
    Ogre::RenderSystem *selectedRenderSystem = NULL;

    do 
    {
      if (c == (int)rsList->size())
        break;
      selectedRenderSystem = rsList->at(c);
      c++;
    } while (selectedRenderSystem->getName().compare("OpenGL Rendering Subsystem")!= 0);

    if (selectedRenderSystem == NULL)
    {
      gzthrow( "unable to find rendering system" );
    }

    selectedRenderSystem->setConfigOption("Full Screen","No");
    selectedRenderSystem->setConfigOption("FSAA","2");

    if (create && this->videoMode != "None")
    {
      selectedRenderSystem->setConfigOption("Video Mode",this->videoMode);
      this->root->setRenderSystem(selectedRenderSystem);
    }
    else
    {
      std::cerr << "No render system selected\n";
    }

  }
}


////////////////////////////////////////////////////////////////////////////////
// Create a window for Ogre
void OgreAdaptor::CreateWindow()
{
  Ogre::StringVector paramsVector;
  Ogre::NameValuePairList params;

  paramsVector.push_back( Ogre::StringConverter::toString( (size_t)(Global::gui->GetDisplay()) ) );
  paramsVector.push_back( Ogre::StringConverter::toString((int)Global::gui->GetVisualInfo()->screen));

  paramsVector.push_back( Ogre::StringConverter::toString((int)Global::gui->GetWindowId()));
  paramsVector.push_back( Ogre::StringConverter::toString((size_t)(Global::gui->GetVisualInfo())));

  params["parentWindowHandle"] = Ogre::StringConverter::toString(paramsVector);

  this->window = this->root->createRenderWindow( "WindowName", Global::gui->GetWidth(), Global::gui->GetHeight(), false, &params);

  this->window->setActive(true);
  this->window->setAutoUpdated(true);
}

////////////////////////////////////////////////////////////////////////////////
// Resize the render window
void OgreAdaptor::ResizeWindow(unsigned int w, unsigned int h)
{
  this->window->resize(w, h);
  this->window->update();

  this->viewport->setDimensions(0,0,1,1);

  this->camera->setAspectRatio( Ogre::Real(viewport->getActualWidth()) / Ogre::Real(viewport->getActualHeight()) );
  //this->frameListener->Resize(w,h);
}


////////////////////////////////////////////////////////////////////////////////
/// Create a light source and attach it to the entity
void OgreAdaptor::CreateLight(XMLConfigNode *node, Entity *entity)
{
  Vector3 vec;
  double range,constant,linear,quad;

  // Create the light
  Ogre::Light *light(this->sceneMgr->createLight(entity->GetName()));

  // Set the light type
  std::string lightType = node->GetString("type","point",0);
  if (lightType == "point")
  {
    light->setType(Ogre::Light::LT_POINT);
  }
  else if (lightType == "directional")
  {
    light->setType(Ogre::Light::LT_DIRECTIONAL);
  }
  else if (lightType == "spot")
  {
    light->setType(Ogre::Light::LT_SPOTLIGHT);
  }

  // Set the diffuse color
  vec = node->GetVector3("diffuseColor",Vector3(1.0, 1.0, 1.0)); 
  light->setDiffuseColour(vec.x, vec.y, vec.z);

  // Sets the specular color
  vec = node->GetVector3("specularColor",Vector3(1.0, 1.0, 1.0)); 
  light->setSpecularColour(vec.x, vec.y, vec.z);

  // Set the direction which the light points
  vec = node->GetVector3("direction", Vector3(0.0, -1.0, 0.0));
  light->setDirection(vec.x, vec.y, vec.z);

  // Absolute range of light in world coordinates
  range = node->GetTupleDouble("attenuation",0,1000);

  // Constant factor. 1.0 means never attenuate, 0.0 is complete attenuation
  constant = node->GetTupleDouble("attenuation",1,1.0);

  // Linear factor. 1 means attenuate evenly over the distance
  linear = node->GetTupleDouble("attenuation",2,0);

  // Quadartic factor.adds a curvature to the attenuation formula
  quad = node->GetTupleDouble("attenuation",3,0);

  // Set attenuation
  light->setAttenuation(range, constant, linear, quad);

  // TODO: More options for Spot lights, etc.

  entity->GetSceneNode()->attachObject(light);
}
 

////////////////////////////////////////////////////////////////////////////////
// Render
int OgreAdaptor::Render()
{
  //Ogre::WindowEventUtilities::messagePump();

  OgreHUD::Instance()->Update();

  root->renderOneFrame();

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Use this function to set the pose of a scene node
void OgreAdaptor::SetSceneNodePose( Ogre::SceneNode *node, const Pose3d &pose )
{
  this->SetSceneNodePosition(node, pose.pos);
  this->SetSceneNodeRotation(node, pose.rot);
}

////////////////////////////////////////////////////////////////////////////////
/// Use this function to set the position of a scene node
void OgreAdaptor::SetSceneNodePosition( Ogre::SceneNode *node, const Vector3 &pos )
{
  node->setPosition(pos.y, pos.z, pos.x);
}

////////////////////////////////////////////////////////////////////////////////
/// Use this function to set the rotation of a scene node
void OgreAdaptor::SetSceneNodeRotation( Ogre::SceneNode *node, const Quatern &rot )
{
  node->setOrientation(rot.u, rot.y, rot.z, rot.x);
}

////////////////////////////////////////////////////////////////////////////////
// Helper function to create a camera
Ogre::Camera *OgreAdaptor::CreateCamera(const std::string &name, double nearClip, double farClip, Ogre::RenderTarget *renderTarget)
{
  Ogre::Camera *camera;
  Ogre::Viewport *cviewport;


  camera = this->sceneMgr->createCamera(name);

  camera->setNearClipDistance(nearClip);
  camera->setFarClipDistance(farClip);

  // Setup the viewport to use the texture
  cviewport = renderTarget->addViewport(camera);
  cviewport->setClearEveryFrame(true);
  cviewport->setBackgroundColour( *this->backgroundColor );
  cviewport->setOverlaysEnabled(false);
  
  camera->setAspectRatio(
      Ogre::Real(cviewport->getActualWidth()) / 
      Ogre::Real(cviewport->getActualHeight()) );

  return camera;
}

////////////////////////////////////////////////////////////////////////////////
// Draw a grid on the ground
void OgreAdaptor::DrawGrid()
{
  /*Ogre::ManualObject* gridObject =  this->sceneMgr->createManualObject("__OGRE_GRID__"); 

  Ogre::SceneNode* gridObjectNode = this->sceneMgr->getRootSceneNode()->createChildSceneNode("__OGRE_GRID_NODE__"); 

  Ogre::MaterialPtr gridObjectMaterialX = Ogre::MaterialManager::getSingleton().create("__OGRE_GRID_MATERIAL_X__","debugger1"); 
  gridObjectMaterialX->setReceiveShadows(true); 
  gridObjectMaterialX->getTechnique(0)->setLightingEnabled(true); 
  gridObjectMaterialX->getTechnique(0)->getPass(0)->setDiffuse(0.4,0.0,0.0,0); 
  gridObjectMaterialX->getTechnique(0)->getPass(0)->setAmbient(0.4,0.0,0.0); 
  gridObjectMaterialX->getTechnique(0)->getPass(0)->setSelfIllumination(0.1,0.0,0.0); 

  Ogre::MaterialPtr gridObjectMaterialY = Ogre::MaterialManager::getSingleton().create("__OGRE_GRID_MATERIAL_Y__","debugger2"); 
  gridObjectMaterialY->setReceiveShadows(true); 
  gridObjectMaterialY->getTechnique(0)->setLightingEnabled(true); 
  gridObjectMaterialY->getTechnique(0)->getPass(0)->setDiffuse(0.0,0.0,0.4,0); 
  gridObjectMaterialY->getTechnique(0)->getPass(0)->setAmbient(0.0,0.0,0.4); 
  gridObjectMaterialY->getTechnique(0)->getPass(0)->setSelfIllumination(0.0,0.0,0.1); 


  float d = 0.01;
  float z = 0.01;
  
  gridObject->begin("__OGRE_GRID_MATERIAL_Y__", Ogre::RenderOperation::OT_TRIANGLE_LIST); 

  for (int y=-100; y<100; y++)
  {
    if (y%10 == 0)
      d = 0.04;
    else
      d = 0.01;

    gridObject->position(100,z, y-d); 
    gridObject->position(-100,z, y-d); 
    gridObject->position(100,z, y+d); 

    gridObject->position(100,z, y+d); 
    gridObject->position(-100,z, y-d); 
    gridObject->position(-100,z, y+d); 

    char *name=new char[20];
    char *text=new char[10];

    sprintf(name,"(%d %d)_yaxis",0,y);
    sprintf(text,"%d",y);
    MovableText* msg = new MovableText(name, text,"Arial",0.08);
    msg->SetTextAlignment(MovableText::H_CENTER, MovableText::V_ABOVE);

    Ogre::SceneNode *textNode = this->sceneMgr->getRootSceneNode()->createChildSceneNode(std::string(name)+"_node");
 
    textNode->attachObject(msg); 
    textNode->translate(0,0.02,y);

    delete name;
    delete text;

  }

  gridObject->end(); 
  gridObject->begin("__OGRE_GRID_MATERIAL_X__", Ogre::RenderOperation::OT_TRIANGLE_LIST); 

  z -= 0.001;

  for (int x=-100; x<100; x++)
  {
    if (x%10 == 0)
      d = 0.04;
    else
      d = 0.01;

    gridObject->position(x-d, z, 100); 
    gridObject->position(x+d, z, 100); 
    gridObject->position(x-d, z, -100); 

    gridObject->position(x+d, z, 100); 
    gridObject->position(x+d, z, -100); 
    gridObject->position(x-d, z, -100); 

    char *name=new char[20];
    char *text=new char[10];

    sprintf(name,"(%d %d)_xaxis",x,0);
    sprintf(text,"%d",x);
    MovableText* msg = new MovableText(name, text,"Arial",0.08);
    msg->SetTextAlignment(MovableText::H_CENTER, MovableText::V_ABOVE);

    Ogre::SceneNode *textNode = this->sceneMgr->getRootSceneNode()->createChildSceneNode(std::string(name)+"_node");
 
    textNode->attachObject(msg); 
    textNode->translate(x,0.02,0);

    delete name;
    delete text;
  }


  
  // etc 
  gridObject->end(); 
  gridObjectNode->attachObject(gridObject);
  */
}
