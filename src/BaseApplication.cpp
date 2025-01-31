/*
-----------------------------------------------------------------------------
Filename:    BaseApplication.cpp
-----------------------------------------------------------------------------
 
This source file is part of the
   ___                 __    __ _ _    _ 
  /___\__ _ _ __ ___  / / /\ \ (_) | _(_)
 //  // _` | '__/ _ \ \ \/  \/ / | |/ / |
/ \_// (_| | | |  __/  \  /\  /| |   <| |
\___/ \__, |_|  \___|   \/  \/ |_|_|\_\_|
      |___/                              
	  Tutorial Framework
	  http://www.ogre3d.org/tikiwiki/
-----------------------------------------------------------------------------
*/
#include "BaseApplication.h"
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <stdio.h>
#include <exception>
#include <math.h>

#include <fstream>
using namespace std;

#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE
#include <macUtils.h>
#endif



 
//-------------------------------------------------------------------------------------
BaseApplication::BaseApplication(void)
	: moving(false),
	  mRoot(0),
	  mSceneMgr(0),
	  mWindow(0),
	  mResourcesCfg(Ogre::StringUtil::BLANK),
	  mPluginsCfg(Ogre::StringUtil::BLANK),
	  mTrayMgr(0),
	  mDetailsPanel(0),
	  mDetailsAppRace(0),
	  mCursorWasVisible(false),
	  mShutDown(false),
	  mInputManager(0),
	  mMouse(0),
	  mKeyboard(0),
	  mPlayer(0),
	  mPlayerBodyNode(0),
	  mOverlaySystem(0),
	  robotModel(0),
      syncedUpdate(false),
	  takeSnapshot(false),
	  videoUpdateL(false),
	  videoUpdateR(false),
	  mapArrived(false),
	  snPos(Ogre::Vector3::ZERO),
	  snOri(Ogre::Quaternion::IDENTITY),
	  vdPosL(Ogre::Vector3::ZERO),
	  vdPosR(Ogre::Vector3::ZERO),
	  vdOriL(Ogre::Quaternion::IDENTITY),
	  vdOriR(Ogre::Quaternion::IDENTITY),
	  hRosSubJoy(NULL),
	  hRosSubMap(NULL),
	  hRosSubRGB(NULL),
	  hRosSubDepth(NULL),
	  rosMsgSync(NULL),
	  rosPTUClient(NULL),
	  ptuSweep(NULL),
	  globalMap(NULL),
	  fbSpeed(0), 
	  lrSpeed(0),
	  testAn(false),
	  changX(0),
	  changY(0),
	  changZ(0)
{
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    m_ResourcePath = Ogre::macBundlePath() + "/Contents/Resources/";
#else
    m_ResourcePath = "";
#endif
}
 
//-------------------------------------------------------------------------------------
BaseApplication::~BaseApplication(void)
{
	// clean up all rendering related components and managers
	if (mTrayMgr) delete mTrayMgr;
	if (mOverlaySystem) delete mOverlaySystem;
	if (snLib) delete snLib;
	if (rsLib) delete rsLib;
	if (globalMap) delete globalMap;

	//Remove ourself as a Window listener
	Ogre::WindowEventUtilities::removeWindowEventListener(mWindow, this);
	windowClosed(mWindow);
	// final engine shutdown
	delete mRoot;
}
 
//-------------------------------------------------------------------------------------
bool BaseApplication::configure(void)
{
	// Show the configuration dialog and initialise the system
	// You can skip this and use root.restoreConfig() to load configuration
	// settings if you were sure there are valid ones saved in ogre.cfg
	if(mRoot->restoreConfig() || mRoot->showConfigDialog())
	{
		// If returned true, user clicked OK so initialise
		// Here we choose to let the system create a default rendering window by passing 'true'
		mWindow = mRoot->initialise(true, "Roculus Render Window");

		std::cout<<"Initializing"<<std::endl; 
		return true;
	}
	else
	{
		return false;
	}
}
//-------------------------------------------------------------------------------------
void BaseApplication::chooseSceneManager(void)
{
	// Get the SceneManager, in this case a generic one
	mSceneMgr = mRoot->createSceneManager(Ogre::ST_GENERIC);

	// initialize the OverlaySystem (changed for 1.9)
	mOverlaySystem = new Ogre::OverlaySystem();
	mSceneMgr->addRenderQueueListener(mOverlaySystem);
}
//-------------------------------------------------------------------------------------
void BaseApplication::createFrameListener(void)
{
	// Set up the management for the input handling
	Ogre::LogManager::getSingletonPtr()->logMessage("*** Initializing OIS ***");
	OIS::ParamList pl;
	size_t windowHnd = 0;
	std::ostringstream windowHndStr;
 
	mWindow->getCustomAttribute("WINDOW", &windowHnd);
	windowHndStr << windowHnd;
	pl.insert(std::make_pair(std::string("WINDOW"), windowHndStr.str()));
 
	mInputManager = OIS::InputManager::createInputSystem( pl );
 
	mKeyboard = static_cast<OIS::Keyboard*>(mInputManager->createInputObject( OIS::OISKeyboard, true ));
	mMouse = static_cast<OIS::Mouse*>(mInputManager->createInputObject( OIS::OISMouse, true ));

	mMouse->setEventCallback(this);
	mKeyboard->setEventCallback(this);
 
	//Set initial mouse clipping size
	windowResized(mWindow);
 
	//Register as a Window listener
	Ogre::WindowEventUtilities::addWindowEventListener(mWindow, this);
 
	mInputContext.mKeyboard = mKeyboard;
	mInputContext.mMouse = mMouse;
	mTrayMgr = new OgreBites::SdkTrayManager("InterfaceName", mWindow, mInputContext, this);
	mTrayMgr->showFrameStats(OgreBites::TL_BOTTOMLEFT);
	mTrayMgr->showLogo(OgreBites::TL_BOTTOMRIGHT);
	mTrayMgr->hideCursor();
 
	// create a params panel for displaying sample details
	Ogre::StringVector items;
	items.push_back("cam.pX");
	items.push_back("cam.pY");
	items.push_back("cam.pZ");

	items.push_back("");
	items.push_back("Filtering");
	items.push_back("Poly Mode");
	items.push_back("Yaw");
	items.push_back("Angle");
 
	mDetailsPanel = mTrayMgr->createParamsPanel(OgreBites::TL_NONE, "DetailsPanel", 250, items);
	mDetailsPanel->setParamValue(4, "vertexColors.material");
	mDetailsPanel->setParamValue(5, "Solid (default)");
	mDetailsPanel->hide();
	
	Ogre::StringVector items_app;
	items_app.push_back("LAPS");
	items_app.push_back("");
	items_app.push_back("Time");
	items_app.push_back("");
	items_app.push_back("Checkpoint");
	mDetailsAppRace = mTrayMgr->createParamsPanel(OgreBites::TL_NONE, "AppPanel", 250, items_app);
	mDetailsAppRace->hide();
	
 
	// register at the Ogre root
	mRoot->addFrameListener(this);
}
//-------------------------------------------------------------------------------------
void BaseApplication::destroyScene(void)
{
	if (robotModel) delete robotModel;
	robotModel = NULL;
	if (mPlayer) delete mPlayer;
	mPlayer = NULL;
	if (oculus) delete oculus;
	oculus = NULL;
	// add stuff here if necessary
}
//-------------------------------------------------------------------------------------

void BaseApplication::setupResources(void)
{
	// Load resource paths from config file
	Ogre::ConfigFile cf;
	cf.load(mResourcesCfg);
 
	// Go through all sections & settings in the file
	Ogre::ConfigFile::SectionIterator seci = cf.getSectionIterator();
 
	Ogre::String secName, typeName, archName;
	while (seci.hasMoreElements())
	{
		secName = seci.peekNextKey();
		Ogre::ConfigFile::SettingsMultiMap *settings = seci.getNext();
		Ogre::ConfigFile::SettingsMultiMap::iterator i;
		for (i = settings->begin(); i != settings->end(); ++i)
		{
			typeName = i->first;
			archName = i->second;
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE
			// OS X does not set the working directory relative to the app,
			// In order to make things portable on OS X we need to provide
			// the loading with it's own bundle path location
			if (!Ogre::StringUtil::startsWith(archName, "/", false)) // only adjust relative dirs
				archName = Ogre::String(Ogre::macBundlePath() + "/" + archName);
#endif
			Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
				archName, typeName, secName);
		}
	}
}
//-------------------------------------------------------------------------------------
void BaseApplication::createResourceListener(void)
{
}
//-------------------------------------------------------------------------------------
void BaseApplication::loadResources(void)
{
	Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();

}
//-------------------------------------------------------------------------------------
void BaseApplication::go(void)
{
	// These are the resources to be loaded:
	#ifdef _DEBUG
		#ifndef OGRE_STATIC_LIB	
			mResourcesCfg = m_ResourcePath + "resources_d.cfg";
			mPluginsCfg = m_ResourcePath + "plugins_d.cfg";
		#else
			mResourcesCfg = "resources_d.cfg";
			mPluginsCfg = "plugins_d.cfg";
		#endif
	#else
		#ifndef OGRE_STATIC_LIB
			mResourcesCfg = m_ResourcePath + "resources.cfg";
			mPluginsCfg = m_ResourcePath + "plugins.cfg";
		#else
			mResourcesCfg = "resources.cfg";
			mPluginsCfg = "plugins.cfg";
		#endif
	#endif
 
	// set everything up
	if (!setup())
		return;

	// on success ROS can be started
	hRosSpinner->start();
	std::cout << " ---> ROS spinning." << std::endl;
	
	/* wait a second to (hopefully) prevent the nasty thread-collision that keeps shutting down the engine */
	boost::posix_time::milliseconds wait(1000);
	boost::this_thread::sleep(wait);
	
	// start the rendering loop
	mRoot->startRendering();

	// on shutdown stop ROS
	hRosSpinner->stop();
	// clean up scene components
	destroyScene();
	// clean up ROS
	destroyROS();
}

//-------------------------------------------------------------------------------------
bool BaseApplication::setup(void)
{
	// Start the Ogre::Root
	mRoot = new Ogre::Root(mPluginsCfg);
 
	// Parse the resource.cfg file
	setupResources();

 
	bool carryOn = configure();
	if (!carryOn) return false;
 
	chooseSceneManager();

	// Set default mipmap level (NB some APIs ignore this)
	Ogre::TextureManager::getSingleton().setDefaultNumMipmaps(3);

	// Create any resource listeners (for loading screens)
	createResourceListener();
	// Load resources
	loadResources();


 
	// Create the scene
	createScene();
 	createFrameListener();

	// initialize the GAME (singleton pattern)
	
	// get a SceneNode for the PlayerBody
	mPlayerBodyNode = mSceneMgr->getRootSceneNode()->createChildSceneNode("PlayerBodyNode");
	
	// scene node for the objective in the race app
	objective = mSceneMgr->getRootSceneNode()->createChildSceneNode("objective");
	objective->attachObject(mSceneMgr->createEntity("Objective"));
	
	// set up the Oculus Rift (and along with it cameras and viewports of the engine)
	oculus = new Oculus();
	oculus->setupOculus();
	oculus->setupOgre(mSceneMgr, mWindow, mPlayerBodyNode);
	
	// set up the components from the master thesis application
	// (wow, that really was some low level of coding I did there...)
	mPlayer = new PlayerBody(mPlayerBodyNode);
	//robotModel = new Robot(mSceneMgr); moving to Roculus CreateScene
	globalMap = new GlobalMap(mSceneMgr);
	
	// configure the ROS setup
	initROS();

	return true;
};
//-------------------------------------------------------------------------------------
bool BaseApplication::frameStarted(const Ogre::FrameEvent& evt)
{

	if(mWindow->isClosed())
		return false;
 

	if(mShutDown)
		return false;
	
	//~ if (syncedUpdate) {
		//~ rsLib->placeInScene(depImage, texImage, snPos, snOri);
		//~ syncedUpdate = false;
	//~ }

	
		
	// std::cout << "vidupdL: " << videoUpdateL << std::endl;
	// std::cout << "vidupdR: " << videoUpdateR << std::endl;
	
	// update video node if necessary
	if (videoUpdateL) {
		
		// but first take a Snapshot, if it was requested
		if (takeSnapshot) {
			snLib->placeInScene(depVideoL, texVideoL, vdPosL, vdOriL);
			takeSnapshot = false;
		}
		vdVideoLeft->update(depVideoL, texVideoL, vdPosL, vdOriL);
		videoUpdateL = false;
	}

	// update video node if necessary
	if (videoUpdateR) {
		
		// but first take a Snapshot, if it was requested
		if (takeSnapshot) {
			snLib->placeInScene(depVideoR, texVideoR, vdPosR, vdOriR);
			takeSnapshot = false;
		}
		vdVideoRight->update(depVideoR, texVideoR, vdPosR, vdOriR);
		videoUpdateR = false;
	}
	
	// insert the map
	if (mapArrived) {
		globalMap->includeMap(mapImage);
		//~ globalMap->flipVisibility();
		mapArrived = false;
	}
	
	return true;
}

bool BaseApplication::frameRenderingQueued(const Ogre::FrameEvent& evt) {
	
	// get the exclusive access to the GAME data
	boost::recursive_mutex::scoped_lock lock(GAME_MUTEX);
	//Need to capture/update each device, JoyStick is handled by ROS
	mKeyboard->capture();
	mMouse->capture();

	mTrayMgr->frameRenderingQueued(evt);


// --- carlos
	robotModel->updateFrom(tfListener); // Update the robot's position and orientation
	
	if (mPlayer->isFirstPerson()) {
		mPlayer->frameRenderingQueued(robotModel, moving);  // first-person mode will mout the player on top of the robot
	} else {
		mPlayer->frameRenderingQueued(evt); // Apply player(~body) movement, if not in first-person
	}

	

	// update the oculus orientation
	oculus->update();
	
	// Publish the angle that has the view compared to the robot 
	std_msgs::Float32 angle;
	float angle_f = (oculus->getOrientation().getYaw() + mPlayerBodyNode->getOrientation().getYaw() - robotModel->getSceneNode()->getOrientation().getYaw())
									.valueRadians() - M_PI/2;
	angle.data = angle_f;
	hRosPubAngle->publish(angle);
	
	app_race->step(tfListener);
	// Update the app/race information
	if(mDetailsAppRace->isVisible()) {
		// Laps
		mDetailsAppRace->setParamValue(0, Ogre::StringConverter::toString(app_race->getLaps())+"/"+Ogre::StringConverter::toString(NUMBER_LAPS));
		// Time (with mm : ss format)
		mDetailsAppRace->setParamValue(2, 
		Ogre::StringConverter::toString(app_race->getTime()/600) + 
		Ogre::StringConverter::toString((app_race->getTime()/60)%10) + 
		" : " +
		Ogre::StringConverter::toString((app_race->getTime()/10)%6) +
		Ogre::StringConverter::toString(app_race->getTime()%10)
		);
		// Current check point
		mDetailsAppRace->setParamValue(4, Ogre::StringConverter::toString(app_race->getCP()));
	}
	
	// update the panel information
	if (mDetailsPanel->isVisible()) {
		mDetailsPanel->setParamValue(0, Ogre::StringConverter::toString(oculus->getCameraNode()->_getDerivedPosition().x));
		mDetailsPanel->setParamValue(1, Ogre::StringConverter::toString(oculus->getCameraNode()->_getDerivedPosition().y));
		mDetailsPanel->setParamValue(2, Ogre::StringConverter::toString(oculus->getCameraNode()->_getDerivedPosition().z));
		
		try {
		double yaw,pitch,roll;	// debug yaw  //
		tf::StampedTransform baseTF;
		tfListener->lookupTransform("global","marker",ros::Time(0), baseTF); //////////////////// carlos
		baseTF.getBasis().getEulerYPR(yaw,pitch,roll);
		mDetailsPanel->setParamValue(6, Ogre::StringConverter::toString(yaw));
		} catch (tf::TransformException ex){
		ROS_ERROR("%s",ex.what());
		}
		
		mDetailsPanel->setParamValue(7, Ogre::StringConverter::toString(angle_f));
		//mDetailsPanel->setParamValue(7, Ogre::StringConverter::toString(moving));
	}
	
	// FLC orders, in case we are in 1st person
	if (mPlayer->isFirstPerson()) {
		
		f_l_controller->moveRobot(fbSpeed, lrSpeed, angle_f);
		
	}

	// for game navigation: NOT NEEDED ANYMORE
	//  update the cursor
	Ogre::Vector3 pos(mPlayerBodyNode->getPosition()+Ogre::Vector3::UNIT_Y*0.7);
	Ogre::Quaternion qView = Ogre::Quaternion(mPlayerBodyNode->getOrientation().getYaw(), Ogre::Vector3::UNIT_Y)*oculus->getOrientation();
	Ogre::Vector3 view(-qView.zAxis());
	if (view.y >= -0.05f) view.y = -0.05f;
	Ogre::Vector3 xzPoint = pos - view*(pos.y/(view.y-0.05f))*0.35f;
	xzPoint.y = 0.05f;
	cursor->setPosition(xzPoint);
	
	return true;
}

bool BaseApplication::frameEnded(const Ogre::FrameEvent& evt) {
	// Lock the framerate and save some processing power
	int dt = 25000 - int(1000000.0*evt.timeSinceLastFrame);
	// ...IF we have the resources...
	if (dt < 0) dt = 0;
	if (dt > 25000) dt = 25000;
	boost::posix_time::microseconds wait(dt);
	boost::this_thread::sleep(wait);
	return true;
}

//---------------------------Event Management------------------------------------------
bool BaseApplication::keyPressed( const OIS::KeyEvent &arg )
{
	static size_t escCounter = 0;
	if (mTrayMgr->isDialogVisible()) return true;   // don't process any more keys if dialog is up
 
	if (arg.key == OIS::KC_F3)   // toggle visibility of advanced frame stats
	{
		mTrayMgr->toggleAdvancedFrameStats();
	}
	else if (arg.key == OIS::KC_F4)   // toggle visibility of even rarer debugging details
	{
		if (mDetailsPanel->getTrayLocation() == OgreBites::TL_NONE)
		{
			mTrayMgr->moveWidgetToTray(mDetailsPanel, OgreBites::TL_TOPRIGHT, 0);
			mDetailsPanel->show();
		}
		else
		{
			mTrayMgr->removeWidgetFromTray(mDetailsPanel);
			mDetailsPanel->hide();
		}
	} else if (arg.key == OIS::KC_F7)   // start app
	{
		if (mDetailsAppRace->getTrayLocation() == OgreBites::TL_NONE)
		{
			mTrayMgr->moveWidgetToTray(mDetailsAppRace, OgreBites::TL_TOPLEFT, 0);
			mDetailsAppRace->show();
			app_race -> start(tfListener);
		}
		else
		{
			mTrayMgr->removeWidgetFromTray(mDetailsAppRace);
			mDetailsAppRace->hide();
			objective->setVisible(false);
		}
	} else if (arg.key == OIS::KC_V) {
		rsLib->flipVisibility();
	}
	else if(arg.key == OIS::KC_F5)   // refresh all textures
	{
		Ogre::TextureManager::getSingleton().reloadAll();
	}
    else if(arg.key == OIS::KC_P)   // refresh all textures
    {
        mPlayer->toggleFirstPersonMode();
    }
    else if(arg.key == OIS::KC_M) {  // toggle map visibility
		globalMap->flipVisibility();
	}
	else if (arg.key == OIS::KC_SYSRQ)   // take a screenshot
	{
		mWindow->writeContentsToTimestampedFile("screenshot", ".jpg");
	}
	else if (arg.key == OIS::KC_ESCAPE) // you have to hit ESC three times to shut down
	{
		escCounter++;
		if (escCounter>=3)
			mShutDown = true;
	}
	/* keys added for the GAME */
	else if (arg.key == OIS::KC_SPACE) {
		//sendNavigationTarget();
		testAn = true;
	} else if (arg.key == OIS::KC_I) {
		
		testAn = false;
		//changX = 0;
		//changY = 0;
		//changZ=0;
		/* Reinitiate the GAME */ 
		//Game::getInstance().startGameSession();
		//escCounter = 0; /// carlos
	} else if (arg.key == OIS::KC_U && testAn) {
		changY = changY +0.01;
	} else if (arg.key == OIS::KC_J && testAn) {
		changY = changY -0.01;
	} else if (arg.key == OIS::KC_H && testAn) {
		changX = changX - 0.01;
	} else if (arg.key == OIS::KC_K && testAn) {
		changX = changX + 0.01;
	} else if (arg.key == OIS::KC_Y && testAn) {
		changZ = changZ + 0.01;
	} else if (arg.key == OIS::KC_T && testAn) {
		changZ = changZ - 0.01;
	} 
	
	// FLC orders
	else if (arg.key == OIS::KC_W) {
		fbSpeed = 1.0;
		moving = true;
		if(lrSpeed != 0) {
			fbSpeed = fbSpeed / sqrt(2);
			lrSpeed = lrSpeed / sqrt(2);		}
		
	} else if (arg.key == OIS::KC_S) {
		fbSpeed = -1.0;
		moving = true;
		if(lrSpeed != 0) {
			fbSpeed = fbSpeed / sqrt(2);
			lrSpeed = lrSpeed / sqrt(2);		}
			
	} else if (arg.key == OIS::KC_A) {
		lrSpeed = 1.0;
		moving = true;
		if(fbSpeed != 0) {
			fbSpeed = fbSpeed / sqrt(2);
			lrSpeed = lrSpeed / sqrt(2);		}
			
	} else if (arg.key == OIS::KC_D) {
		lrSpeed = -1.0;
		moving = true;
		if(fbSpeed != 0) {
			fbSpeed = fbSpeed / sqrt(2);
			lrSpeed = lrSpeed / sqrt(2);		}
			
	}

	mPlayer->injectKeyDown(arg);
	return true;
}
 
bool BaseApplication::keyReleased( const OIS::KeyEvent &arg )
{
  mPlayer->injectKeyUp(arg);
  
  if (arg.key == OIS::KC_W) {
		fbSpeed = 0.0;
		if(lrSpeed != 0) {
			lrSpeed = lrSpeed * sqrt(2);		
		} else {
			moving = false;		}
		
	} else if (arg.key == OIS::KC_S) {
		fbSpeed = 0.0;
		if(lrSpeed != 0) {
			lrSpeed = lrSpeed * sqrt(2);			
		} else {
			moving = false;		}
			
	} else if (arg.key == OIS::KC_A) {
		lrSpeed = 0.0;
		if(fbSpeed != 0) {
			fbSpeed = fbSpeed * sqrt(2);			
		} else {
			moving = false;		}
			
	} else if (arg.key == OIS::KC_D) {
		lrSpeed = 0.0;
		if(fbSpeed != 0) {
			fbSpeed = fbSpeed * sqrt(2);			
		} else {
			moving = false;		}
			
	}
  return true;
}
 
bool BaseApplication::mouseMoved( const OIS::MouseEvent &arg )
{
    if (mTrayMgr->injectMouseMove(arg)) return true;
	return true;
}
 
bool BaseApplication::mousePressed( const OIS::MouseEvent &arg, OIS::MouseButtonID id )
{
	//sendNavigationTarget();
    if (mTrayMgr->injectMouseDown(arg, id)) return true;
	return true;
}
 
bool BaseApplication::mouseReleased( const OIS::MouseEvent &arg, OIS::MouseButtonID id )
{
    if (mTrayMgr->injectMouseUp(arg, id)) return true;
	return true;
}


 
void BaseApplication::windowResized(Ogre::RenderWindow* rw)
{
	//Adjust mouse clipping area
	unsigned int width, height, depth;
	int left, top;
	rw->getMetrics(width, height, depth, left, top);
 
	const OIS::MouseState &ms = mMouse->getMouseState();
	ms.width = width;
	ms.height = height;
}
 

void BaseApplication::windowClosed(Ogre::RenderWindow* rw)
{
	//Unattach OIS before window shutdown (very important under Linux)
	//Only close for window that created OIS (the main window in these demos)
	if( rw == mWindow )
	{
		if( mInputManager )
		{
			mInputManager->destroyInputObject( mMouse );
			mInputManager->destroyInputObject( mKeyboard );
			OIS::InputManager::destroyInputSystem(mInputManager);
			mInputManager = 0;
		}
	}

	
}


void BaseApplication::syncTwoCams(const sensor_msgs::CompressedImageConstPtr& depthImgLeft, const sensor_msgs::CompressedImageConstPtr& rgbImgLeft, 
									const sensor_msgs::CompressedImageConstPtr& depthImgRight, const sensor_msgs::CompressedImageConstPtr& rgbImgRight) {
										
		syncVideoCallback(depthImgLeft, rgbImgLeft, true);
		syncVideoCallback(depthImgRight, rgbImgRight, false);
										
}

void BaseApplication::syncVideoCallback(const sensor_msgs::CompressedImageConstPtr& depthImg, const sensor_msgs::CompressedImageConstPtr& rgbImg, bool is_left) {
	/* Receive a depth-rgb pair of images, filter and convert them into the Ogre format and fetch the according transformation
	 * in order to complete a valid Snapshot */
	 
	// std::cout << "syncCamera " << is_left << std::endl;

	 // used for the coordinate tranformation from ROS to Ogre
	static tfScalar yaw,pitch,roll;
	static tf::StampedTransform vdTransform;
	static Ogre::Matrix3 mRot;

	// std::cout << "videoUpdateR: " << videoUpdateR << " - condition is: " << (!videoUpdateR && !is_left)<< std::endl;
	// std::cout << "whole cond: " <<	((!videoUpdateL && is_left) || (!videoUpdateR && !is_left)) << std::endl;
	 // Only if the last update was rendered
	if ((!videoUpdateL && is_left) || (!videoUpdateR && !is_left)) {			
		try {
			

			

			if(is_left) {
				
			// We have to cut away the compression header to load the depth image into openCV
			compressed_depth_image_transport::ConfigHeader compressionConfig;
			memcpy(&compressionConfig, &depthImg->data[0], sizeof(compressionConfig));
			const std::vector<uint8_t> depthData(depthImg->data.begin() + sizeof(compressionConfig), depthImg->data.end());
			
			// load the images:
			cv::Mat tmp_depth = cv::imdecode(cv::Mat(depthData), CV_LOAD_IMAGE_UNCHANGED);
			cv::Mat tmp_rgb = cv::imdecode(cv::Mat(rgbImg->data), CV_LOAD_IMAGE_UNCHANGED);
			tmp_depth.convertTo(cv_depth_l, CV_16U);
			tmp_rgb.convertTo(cv_rgb_l, CV_8UC3);
			
			// process images, by bluring the depth and rearranging the color values
			cv::GaussianBlur(cv_depth_l, cv_depth_l, cv::Size(11,11), 0, 0);
			cv::cvtColor(cv_rgb_l,cv_rgb_l, CV_BGR2RGB);



			
			/* lookup the transform and convert them to the OGRE coordinates
			 *  unfortunately there is still some magic going on in Video3D.cpp and Snapshot.cpp
			 *  in order to end up in the correct orientation...
			 */
				//tfListener->lookupTransform("camera_left", "camera_left", depthImg->header.stamp, vdTransform);
				tfListener->lookupTransform("map", "cam_left", ros::Time(0), vdTransform);
				
				// positioning
				vdPosL.x = vdTransform.getOrigin().x();
				vdPosL.y = vdTransform.getOrigin().y();
				vdPosL.z = vdTransform.getOrigin().z();
				
				
				// rotation 
				tf::Matrix3x3 tfMat(vdTransform.getBasis());
				tf::Vector3 row0(tfMat.getRow(0)), row1(tfMat.getRow(1)), row2(tfMat.getRow(2));
				Matrix3 rot(row0.x(),row0.y(),row0.z(),row1.x(),row1.y(),row1.z(),row2.x(),row2.y(),row2.z());
				Quaternion quat(rot);
				
				vdOriL = quat;

				
				/* connect the data to the images, note that this does in fact not load, but store pointers instead
				 * which is why the cv_depth and cv_rgb are members of BaseApplication to prevent data loss
				*/
				depVideoL.loadDynamicImage(static_cast<uchar*>(cv_depth_l.data), cv_depth_l.cols, cv_depth_l.rows, 1, Ogre::PF_L16);
				texVideoL.loadDynamicImage(static_cast<uchar*>(cv_rgb_l.data), cv_rgb_l.cols, cv_rgb_l.rows, 1, Ogre::PF_BYTE_RGB);
				videoUpdateL = true;
			} else {
				// We have to cut away the compression header to load the depth image into openCV
			compressed_depth_image_transport::ConfigHeader compressionConfig;
			memcpy(&compressionConfig, &depthImg->data[0], sizeof(compressionConfig));
			const std::vector<uint8_t> depthData(depthImg->data.begin() + sizeof(compressionConfig), depthImg->data.end());
			
			// load the images:
			cv::Mat tmp_depth = cv::imdecode(cv::Mat(depthData), CV_LOAD_IMAGE_UNCHANGED);
			cv::Mat tmp_rgb = cv::imdecode(cv::Mat(rgbImg->data), CV_LOAD_IMAGE_UNCHANGED);
			tmp_depth.convertTo(cv_depth_r, CV_16U);
			tmp_rgb.convertTo(cv_rgb_r, CV_8UC3);
			
			// process images, by bluring the depth and rearranging the color values
			cv::GaussianBlur(cv_depth_r, cv_depth_r, cv::Size(11,11), 0, 0);
			cv::cvtColor(cv_rgb_r,cv_rgb_r, CV_BGR2RGB);
			
			
			
				
			/* lookup the transform and convert them to the OGRE coordinates
			 *  unfortunately there is still some magic going on in Video3D.cpp and Snapshot.cpp
			 *  in order to end up in the correct orientation...
			 */
				
				tfListener->lookupTransform("map", "cam_right", ros::Time(0), vdTransform);
				// positioning
				
				vdPosR.x = vdTransform.getOrigin().x()+changX;
				vdPosR.y = vdTransform.getOrigin().y()+changY;
				vdPosR.z = vdTransform.getOrigin().z()+changZ;
				
				
				//tfListener->lookupTransform("map", "camera_right", depthImg->header.stamp, vdTransform);
				
				// rotation 
				tf::Matrix3x3 tfMat(vdTransform.getBasis());
				tf::Vector3 row0(tfMat.getRow(0)), row1(tfMat.getRow(1)), row2(tfMat.getRow(2));
				Matrix3 rot(row0.x(),row0.y(),row0.z(),row1.x(),row1.y(),row1.z(),row2.x(),row2.y(),row2.z());
				Quaternion quat(rot);
				
				vdOriR = quat;
				
				
				
				/// END
				
				/// USING CALIBRATION (works)
				/* tfListener->lookupTransform("camera_left", "camera_right", ros::Time(0), vdTransform);
				// positioning 
				if(!testAn){
				vdPosR.x = -vdTransform.getOrigin().x();
				vdPosR.y = -vdTransform.getOrigin().y();
				vdPosR.z = vdTransform.getOrigin().z() + OFFSET_Z;
				}else{
				vdPosR.x = changX;
				vdPosR.y = changY;
				vdPosR.z = changZ;}
				// rotation (at least get it into global coords that are fixed on the robot)
				vdTransform.getBasis().getEulerYPR(yaw,pitch,roll);
				
				//if(!testAn)
				//mRot.FromEulerAnglesXYZ(-Radian(roll),Radian(pitch),Radian(yaw));
				//else
				mRot.FromEulerAnglesXYZ(-Radian(roll),Radian(pitch),Radian(yaw));
				vdOriR.FromRotationMatrix(mRot);*/
				/// END
				

				/* connect the data to the images, note that this does in fact not load, but store pointers instead
				 * which is why the cv_depth and cv_rgb are members of BaseApplication to prevent data loss
				*/
				depVideoR.loadDynamicImage(static_cast<uchar*>(cv_depth_r.data), cv_depth_r.cols, cv_depth_r.rows, 1, Ogre::PF_L16);
				texVideoR.loadDynamicImage(static_cast<uchar*>(cv_rgb_r.data), cv_rgb_r.cols, cv_rgb_r.rows, 1, Ogre::PF_BYTE_RGB);
				videoUpdateR = true;
				
				/**std::cout << " roll " << roll << " pitch " << pitch <<  " yaw " << yaw<< std::endl;
				std::cout << " changX " << changX << " changY " << changY <<  " changZ " << changZ<< std::endl;
				std::cout << " -vdTransform.getOrigin().x() " << -vdTransform.getOrigin().x() << " -vdTransform.getOrigin().y() " 
				<< -vdTransform.getOrigin().y() <<  " vdTransform.getOrigin().z() " << vdTransform.getOrigin().z() << std::endl;*/
			}
			
			
		} catch (tf::TransformException ex) {
			ROS_ERROR("%s",ex.what());
			if(is_left) {
				videoUpdateL = false;
			} else {
				videoUpdateR = false;
			}
		} catch (std::exception& e) {
			std::cerr << e.what() << std::endl;
			if(is_left) {
				videoUpdateL = false;
			} else {
				videoUpdateR = false;
			}
		}
	}
}

void BaseApplication::joyCallback(const sensor_msgs::Joy::ConstPtr &joy ) {
	/*
	 * the static variables prevent jitter and repetitive commands
	 * */
	static bool l_button0 = false; 
	static bool l_button1 = false;
	static bool l_button2 = false;
	static bool l_button3 = false;
	static bool l_button5 = false;
	static bool l_button9 = false;
	
	// pass input on to player movements
	mPlayer->injectROSJoy(joy);
	
	if (l_button0 == false && joy->buttons[0] != 0 && takeSnapshot == false) {
		// request recording of a Snapshot
		//~ takeSnapshot = true;
		//sendNavigationTarget();
	}
	else if (l_button1 == false && joy->buttons[1] != 0) {
		// make all manually taken Snapshots invisible (effectively you can record a second set of images)
		//~ snLib->flipVisibility();
		//sendNavigationTarget();
	}
	else if (l_button2 == false && joy->buttons[2] != 0) {
		// make all room sweep Snapshots invisible (effectively you can record a second set of images)
		//~ rsLib->flipVisibility();
		//sendNavigationTarget();
	}
	else if (l_button3 == false && joy->buttons[3] != 0) {
		// trigger a room sweep
		//~ boost::thread tmpThread(boost::bind(&BaseApplication::triggerPanoramaPTUScan, this));
		//sendNavigationTarget();
	}
	else if (l_button5 == false && joy->buttons[5] != 0) {
		// switch between first person and free viewpoint
		//~ mPlayer->toggleFirstPersonMode();
	}
	else if (joy->buttons[7] != 0) {
		// set the oculus orientation back to IDENTITY (effectively looking into the direction the PlayerBody has)
		oculus->resetOrientation();
	}
	//~ else if (l_button9 == false && joy->buttons[9] != 0) {
		//~ // toggle the map
		//~ globalMap->flipVisibility();
	//~ }
	
	// make sure that button is released before triggering an event repeatedly
	l_button0 = (joy->buttons[0] != 0);
	l_button1 = (joy->buttons[1] != 0);
	l_button2 = (joy->buttons[2] != 0);
	l_button3 = (joy->buttons[3] != 0);
	l_button5 = (joy->buttons[5] != 0);
	l_button9 = (joy->buttons[9] != 0);
}

void BaseApplication::mapCallback(const nav_msgs::OccupancyGrid::ConstPtr& map) {
	/* Still the old code version with a memory leak (but is only triggered once) */
	Ogre::MemoryDataStream dataStream(map->data.size(), false, false);
	Ogre::uint8 value(0);
	for (int i=0; i<map->data.size(); i++) {
		if (map->data[i] == -1) {
			value = 40;
		} else {
			value = 2*map->data[i];
		}
		dataStream.write(&value, sizeof(Ogre::uint8));
	}
	dataStream.seek(0);
	
	globalMap->insertWHR(map->info.width, map->info.height, map->info.resolution);
	globalMap->setOrigin(Vector3(map->info.origin.position.y, map->info.origin.position.z, -map->info.origin.position.x));

	Ogre::DataStreamPtr *pMap = new Ogre::DataStreamPtr(&dataStream);
	mapImage.loadRawData(*pMap, map->info.width, map->info.height, Ogre::PF_L8);
	hRosSubMap->shutdown();
	mapArrived = true;
}


void BaseApplication::initROS() {
  int argc = 0;
  char** argv = NULL;
  ros::init(argc, argv, "roculus");
  hRosNode = new ros::NodeHandle();

  f_l_controller 	= new FLC();
  app_race			= new App(mSceneMgr, objective);

  /* Publish the angle of the robot */
  hRosPubAngle = new ros::Publisher(hRosNode->advertise<std_msgs::Float32>("angle", 5));
  
  /* Subscribe to the joystick input */
  hRosSubJoy = new ros::Subscriber(hRosNode->subscribe<sensor_msgs::Joy>
				("/joy/visualization", 10, boost::bind(&BaseApplication::joyCallback, this, _1)));

  /* Subscribe for the map topic (Published Once per Subscriber) and navigation topics */
  hRosSubMap = new ros::Subscriber(hRosNode->subscribe<nav_msgs::OccupancyGrid>
				("/map", 1, boost::bind(&BaseApplication::mapCallback, this, _1)));

  hRosSubRGBVidL = new message_filters::Subscriber<sensor_msgs::CompressedImage>
				(*hRosNode, "/camera1/rgb/image/compressed", 1);
  hRosSubDepthVidL = new message_filters::Subscriber<sensor_msgs::CompressedImage>
				(*hRosNode, "/camera1/depth/image_raw/compressedDepth", 1);

  hRosSubRGBVidR = new message_filters::Subscriber<sensor_msgs::CompressedImage>
				(*hRosNode, "/camera2/rgb/image/compressed", 1);
  hRosSubDepthVidR = new message_filters::Subscriber<sensor_msgs::CompressedImage>
				(*hRosNode, "/camera2/depth/image_raw/compressedDepth", 1);
				
  rosVideoSync = new message_filters::Synchronizer<ApproximateTimePolicy>
				(ApproximateTimePolicy(15), *hRosSubDepthVidL, *hRosSubRGBVidL, *hRosSubDepthVidR, *hRosSubRGBVidR);
  rosVideoSync->registerCallback(boost::bind(&BaseApplication::syncTwoCams, this, _1, _2, _3, _4));

  /*rosVideoSyncL = new message_filters::Synchronizer<ApproximateTimePolicy>
				(ApproximateTimePolicy(15), *hRosSubDepthVidL, *hRosSubRGBVidL);
  rosVideoSyncL->registerCallback(boost::bind(&BaseApplication::syncVideoCallback, this, _1, _2, true));

  rosVideoSyncR = new message_filters::Synchronizer<ApproximateTimePolicy>
				(ApproximateTimePolicy(15), *hRosSubDepthVidR, *hRosSubRGBVidR);
  rosVideoSyncR->registerCallback(boost::bind(&BaseApplication::syncVideoCallback, this, _1, _2, false));*/

  
  /* Setting up the tfListener */
  tfListener = new tf::TransformListener();
  
  /* AsyncSpinner to process msgs. in a separate thread (param =!= 1) */
  hRosSpinner = new ros::AsyncSpinner(1);
}

void BaseApplication::destroyROS() {
	// shutdown ROS and free all memory, if necessary
  ros::shutdown();
  if (hRosSpinner) {
    delete hRosSpinner;
    hRosSpinner = NULL;
  }
  if (rosMsgSync) {
    delete rosMsgSync;
    rosMsgSync = NULL;
  }
  if (rosVideoSyncL) {
    delete rosVideoSyncL;
    rosVideoSyncL = NULL;
  }

  if (rosVideoSyncR) {
    delete rosVideoSyncR;
    rosVideoSyncR = NULL;
  }
  if (hRosSubJoy) {
	delete hRosSubJoy;
	hRosSubJoy = NULL;
  }
  if (hRosSubMap) {
	delete hRosSubMap;
	hRosSubMap = NULL;
  }
  if (hRosSubRGB) {
    delete hRosSubRGB;
    hRosSubRGB = NULL;
  }
  if (hRosSubDepth) {
    delete hRosSubDepth;
    hRosSubDepth = NULL;
  }
  if (hRosSubRGBVidL) {
    delete hRosSubRGB;
    hRosSubRGB = NULL;
  }
  if (hRosSubRGBVidR) {
    delete hRosSubRGB;
    hRosSubRGB = NULL;
  }
  if (hRosSubDepthVidL) {
    delete hRosSubDepth;
    hRosSubDepth = NULL;
  }
  if (hRosSubDepthVidR) {
    delete hRosSubDepth;
    hRosSubDepth = NULL;
  }
  if (hRosNode) {
    delete hRosNode;
    hRosNode = NULL;
  }
  if (rosPTUClient) {
	delete rosPTUClient;
	rosPTUClient = NULL;
  }
}

