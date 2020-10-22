/*
  ==============================================================================

	GenericOSCQueryModule.cpp
	Created: 28 Feb 2019 10:33:17pm
	Author:  bkupe

  ==============================================================================
*/

#include "GenericOSCQueryModule.h"
#include "ui/OSCQueryModuleEditor.h"
#include "GenericOSCQueryCommand.h"
#include "OSCInputHelper.h"

GenericOSCQueryModule::GenericOSCQueryModule(const String& name, int defaultRemotePort) :
	Module(name),
	Thread("OSCQuery"),
	useLocal(nullptr),
	remoteHost(nullptr),
	remotePort(nullptr),
	hasListenExtension(false)
{
	alwaysShowValues = true;
	canHandleRouteValues = true;

	includeValuesInSave = true;

	setupIOConfiguration(true, true);

	keepValuesOnSync = moduleParams.addBoolParameter("Keep Values On Sync", "If checked, this will force keeping the current values when syncing the OSCQuery remote data structure.", false);
	syncTrigger = moduleParams.addTrigger("Sync Data", "Sync the data");
	serverName = moduleParams.addStringParameter("Server Name", "The name of the OSCQuery server, if provided", "");
	serverName->setControllableFeedbackOnly(true);
	listenAllTrigger = moduleParams.addTrigger("Listen to all", "This will automatically enable listen to all containers");

	sendCC.reset(new OSCQueryOutput(this));
	moduleParams.addChildControllableContainer(sendCC.get());
	
	useLocal = sendCC->addBoolParameter("Local", "Send to Local IP (127.0.0.1). Allow to quickly switch between local and remote IP.", true);
	remoteHost = sendCC->addStringParameter("Remote Host", "Remote Host to send to.", "127.0.0.1");
	remoteHost->autoTrim = true;
	remoteHost->setEnabled(!useLocal->boolValue());
	remotePort = sendCC->addIntParameter("Remote port", "Port on which the remote host is listening to", defaultRemotePort, 1, 65535);
	remoteOSCPort = sendCC->addIntParameter("Custom OSC Port", "If enabled, this will override the port to send OSC to, default is sending to the OSCQuery port", defaultRemotePort, 1, 65535);
	remoteOSCPort->canBeDisabledByUser = true;
	remoteOSCPort->setEnabled(false);

	//Script
	scriptObject.setMethod("send", GenericOSCQueryModule::sendOSCFromScript);

	defManager->add(CommandDefinition::createDef(this, "", "Set Value", &GenericOSCQueryCommand::create, CommandContext::BOTH));

	sender.connect("0.0.0.0", 0);

}

GenericOSCQueryModule::~GenericOSCQueryModule()
{
	if (wsClient != nullptr) wsClient->stop(); 
	signalThreadShouldExit();
	waitForThreadToExit(2000);
}

void GenericOSCQueryModule::setupWSClient()
{
	if (wsClient != nullptr) wsClient->stop();
	wsClient.reset();
	if (isCurrentlyLoadingData) return;

	if (!enabled->intValue() || !hasListenExtension) return;
	NLOG(niceName, "Server has LISTEN extension, setting up websocket");
	wsClient.reset(new SimpleWebSocketClient());
	wsClient->addWebSocketListener(this);
	wsClient->start(remoteHost->stringValue()+":"+remotePort->stringValue()+"/");
}

void GenericOSCQueryModule::sendOSCMessage(OSCMessage m)
{
	if (!enabled->boolValue()) return;

	if (logOutgoingData->boolValue())
	{
		NLOG(niceName, "Send OSC : " << m.getAddressPattern().toString());
		for (auto& a : m) LOG(OSCHelpers::getStringArg(a));
	}

	outActivityTrigger->trigger();

	sender.sendToIPAddress(remoteHost->stringValue(), remoteOSCPort->enabled ? remoteOSCPort->intValue() : remotePort->intValue(), m);
}

void GenericOSCQueryModule::sendOSCForControllable(Controllable* c)
{
	if (!enabled->boolValue()) return;

	String s = c->getControlAddress(&valuesCC);
	try
	{
		OSCMessage m(s);
		if (c->type != Controllable::TRIGGER)
		{
			Parameter* p = (Parameter*)c;
			if (p->value.isArray() && p->type != Controllable::COLOR)
			{
				for (int i = 0; i < p->value.size(); ++i)
				{
					m.addArgument(OSCHelpers::varToArgument(p->value[i]));
				}
			}
			else
			{
				m.addArgument(OSCHelpers::varToArgument(p->value));
			}
		}
		sendOSCMessage(m);
	}
	catch (OSCFormatError & e)
	{
		NLOGERROR(niceName, "Can't send to address " << s << " : " << e.description);
	}
}

var GenericOSCQueryModule::sendOSCFromScript(const var::NativeFunctionArgs& a)
{
	GenericOSCQueryModule* m = getObjectFromJS<GenericOSCQueryModule>(a);
	if (!m->enabled->boolValue()) return var();

	if (a.numArguments == 0) return var();

	try
	{
		OSCMessage msg(a.arguments[0].toString());

		for (int i = 1; i < a.numArguments; ++i)
		{
			if (a.arguments[i].isArray())
			{
				Array<var>* arr = a.arguments[i].getArray();
				for (auto& aa : *arr) msg.addArgument(varToArgument(aa));
			}
			else
			{
				msg.addArgument(varToArgument(a.arguments[i]));
			}
		}

		m->sendOSCMessage(msg);
	}
	catch (OSCFormatError & e)
	{
		NLOGERROR(m->niceName, "Error sending message : " << e.description);
	}


	return var();
}


OSCArgument GenericOSCQueryModule::varToArgument(const var& v)
{
	if (v.isBool()) return OSCArgument(((bool)v) ? 1 : 0);
	else if (v.isInt()) return OSCArgument((int)v);
	else if (v.isInt64()) return OSCArgument((int)v);
	else if (v.isDouble()) return OSCArgument((float)v);
	else if (v.isString()) return OSCArgument(v.toString());
	jassert(false);
	return OSCArgument("error");
}

void GenericOSCQueryModule::syncData()
{
	if (isCurrentlyLoadingData) return;

	startThread();
}

void GenericOSCQueryModule::createTreeFromData(var data)
{
	if (data.isVoid()) return;

	Array<String> enableListenContainers;
	Array<String> expandedContainers;
	Array<WeakReference<ControllableContainer>> containers = valuesCC.getAllContainers(true);

	if (!keepValuesOnSync->boolValue())
	{
		for (auto& cc : containers)
		{
			if (GenericOSCQueryValueContainer* gcc = dynamic_cast<GenericOSCQueryValueContainer*>(cc.get()))
			{
				if (gcc->enableListen->boolValue()) enableListenContainers.add(gcc->getControlAddress(&valuesCC));
				if (!gcc->editorIsCollapsed) expandedContainers.add(gcc->getControlAddress(&valuesCC));
			}
		}
	}

	var vData = valuesCC.getJSONData();
	valuesCC.clear();
	fillContainerFromData(&valuesCC, data);
	if (keepValuesOnSync->boolValue())
	{
		if(!vData.isVoid()) valuesCC.loadJSONData(vData);
	}
	else
	{
		for (auto& addr : enableListenContainers)
		{
			if (GenericOSCQueryValueContainer* gcc = dynamic_cast<GenericOSCQueryValueContainer*>(valuesCC.getControllableContainerForAddress(addr)))
			{
				gcc->enableListen->setValue(true);
			}
		}

		for (auto& addr : expandedContainers)
		{
			if (ControllableContainer * cc = valuesCC.getControllableContainerForAddress(addr))
			{
				cc->editorIsCollapsed = false;
			}
		}
	}

	treeData = data;
}

void GenericOSCQueryModule::fillContainerFromData(ControllableContainer* cc, var data)
{
	DynamicObject* dataObject = data.getProperty("CONTENTS", var()).getDynamicObject();
	if (dataObject != nullptr)
	{
		NamedValueSet nvSet = dataObject->getProperties();
		for (auto& nv : nvSet)
		{
			//int access = nv.value.getProperty("ACCESS", 1);
			bool isGroup = /*access == 0 || */nv.value.hasProperty("CONTENTS");
			if (isGroup) //group
			{
				String ccNiceName = nv.value.getProperty("DESCRIPTION", "");
				if (ccNiceName.isEmpty()) ccNiceName = nv.name.toString();

				GenericOSCQueryValueContainer* childCC = new GenericOSCQueryValueContainer(ccNiceName);
				childCC->saveAndLoadRecursiveData = true;
				childCC->setCustomShortName(nv.name.toString());
				fillContainerFromData(childCC, nv.value);
				childCC->editorIsCollapsed = true;

				cc->addChildControllableContainer(childCC, true);
			}
			else
			{
				Controllable* c = createControllableFromData(nv.name, nv.value);
				if (c != nullptr) cc->addControllable(c);
			}
		}
	}
}

Controllable* GenericOSCQueryModule::createControllableFromData(StringRef name, var data)
{
	Controllable* c = nullptr;

	String cNiceName = data.getProperty("DESCRIPTION", "");
	if (cNiceName.isEmpty()) cNiceName = name;

	String type = data.getProperty("TYPE", "").toString();
	var valRange = data.hasProperty("RANGE") ? data.getProperty("RANGE", var()) : var();
	var val = data.getProperty("VALUE", var());
	int access = data.getProperty("ACCESS", 3);

	var value;
	var range;

	if (val.isArray()) value = val;
	else value.append(val);
	if (valRange.isArray()) range = valRange;
	else range.append(valRange);

	if (range.size() != value.size())
	{
		//DBG("Not the same : " << range.size() << " / " << value.size() << "\n" << data.toString());
		//NLOGWARNING(niceName, "RANGE and VALUE fields don't have the same size, skipping : " << cNiceName);
	}
	var minVal;
	var maxVal;
	for (int i = 0; i < range.size(); ++i)
	{
		minVal.append(range[i].getProperty("MIN", INT32_MIN));
		maxVal.append(range[i].getProperty("MAX", INT32_MAX));
	}

	if (type == "N" || type == "I")
	{
		c = new Trigger(cNiceName, cNiceName);
	}
	else if (type == "i" || type == "h")
	{
		c = new IntParameter(cNiceName, cNiceName, value[0], minVal[0], maxVal[0]);
	}
	else if (type == "f" || type == "d")
	{
		c = new FloatParameter(cNiceName, cNiceName, value[0], minVal[0], maxVal[0]);
	}
	else if (type == "ii" || type == "ff"  || type == "hh"  || type == "dd")
	{
        if(value.isVoid()) for(int i=0;i<2; ++i) value.append(0);
		c = new Point2DParameter(cNiceName, cNiceName);
		if (value.size() >= 2) ((Point2DParameter*)c)->setValue(value);
		if (range.size() >= 2) ((Point2DParameter*)c)->setRange(minVal, maxVal);
	}
	else if (type == "iii" || type == "fff" || type == "hhh" || type == "ddd")
	{
		if (value.isVoid()) for (int i = 0; i < 3; ++i) value.append(0);
        c = new Point3DParameter(cNiceName, cNiceName);
		if(value.size() >= 3) ((Point3DParameter*)c)->setValue(value);
		if(range.size() >= 3) ((Point3DParameter*)c)->setRange(minVal, maxVal);
	}
	else if (type == "ffff" || type == "dddd")
	{
		Colour col = value.size() >= 4 ? Colour::fromFloatRGBA(value[0], value[1], value[2], value[3]) : Colours::black;
        c = new ColorParameter(cNiceName, cNiceName, col);
	}
	else if (type == "iiii" || type == "hhhh")
	{
		Colour col = value.size() >= 4 ? Colour::fromRGBA((int)value[0], (int)value[1], (int)value[2], (int)value[3]) : Colours::black;
        c = new ColorParameter(cNiceName, cNiceName, col);
	}
	else if (type == "s" || type == "S"  || type == "c")
	{
		if (range[0].isObject()) //enum
		{
			var options = range[0].getProperty("VALS", var());

			if (options.isArray())
			{
				EnumParameter* ep = new EnumParameter(cNiceName, cNiceName);
				for (int i = 0; i < options.size(); ++i) ep->addOption(options[i], options[i], false);
				ep->setValueWithKey(value[0]);

				c = ep;
			}
		}
		else
		{
			c = new StringParameter(cNiceName, cNiceName, value[0]);
		}
	}
	else if (type == "r")
	{
		Colour col = Colour::fromString(value[0].toString());
		Colour goodCol = Colour(col.getAlpha(), col.getRed(), col.getGreen(), col.getBlue()); //inverse RGBA > ARGB
		c = new ColorParameter(cNiceName, cNiceName, goodCol);
	}
	else if (type == "T" || type == "F")
	{
		c = new BoolParameter(cNiceName, cNiceName, value[0]);
	}

	if (c != nullptr)
	{
		c->setCustomShortName(name);
		if (access == 1) c->setControllableFeedbackOnly(true);
	}

	return c;
}

void GenericOSCQueryModule::updateListenToContainer(GenericOSCQueryValueContainer* gcc)
{
	if (!enabled->boolValue() || !hasListenExtension || isCurrentlyLoadingData) return;
	if (wsClient == nullptr || !wsClient->isConnected)
	{
		NLOGWARNING(niceName, "Websocket not connected, can't LISTEN");
		return;
	}

	String command = gcc->enableListen->boolValue() ? "LISTEN" : "IGNORE";
	Array<WeakReference<Parameter>> params = gcc->getAllParameters();
	
	var o(new DynamicObject());
	o.getDynamicObject()->setProperty("COMMAND", command);
	
	for (auto& p : params)
	{
		if (p == gcc->enableListen) continue;
		String addr = p->getControlAddress(&valuesCC);
		o.getDynamicObject()->setProperty("DATA", addr);
		wsClient->send(JSON::toString(o, true));
	}

}

void GenericOSCQueryModule::onControllableFeedbackUpdateInternal(ControllableContainer* cc, Controllable* c)
{
	Module::onControllableFeedbackUpdateInternal(cc, c);
	
	if (c == useLocal)
	{
		remoteHost->setEnabled(!useLocal->boolValue());
	}
	else if (c == enabled || c == syncTrigger || c == remoteHost || c == remotePort)
	{
		syncData();
	}
	else if (cc == &valuesCC)
	{
		if (GenericOSCQueryValueContainer* gcc = c->getParentAs<GenericOSCQueryValueContainer>())
		{
			if (c == gcc->enableListen)
			{
				updateListenToContainer(gcc);
			}
			else
			{
				sendOSCForControllable(c);
			}
		}
		else
		{
			sendOSCForControllable(c);
		}
	}
	else if (c == listenAllTrigger)
	{
		if (hasListenExtension)
		{
			Array<WeakReference<ControllableContainer>> containers = valuesCC.getAllContainers(true);
			for (auto& cc : containers)
			{
				if (GenericOSCQueryValueContainer* gcc = dynamic_cast<GenericOSCQueryValueContainer*>(cc.get())) gcc->enableListen->setValue(true);
			}
		}
	}
}

void GenericOSCQueryModule::connectionOpened()
{
	NLOG(niceName, "Websocket connection is opened, let's get bi, baby !");
}

void GenericOSCQueryModule::connectionClosed(int status, const String& reason)
{
	NLOG(niceName, "Websocket connection is closed, bye bye!");
}

void GenericOSCQueryModule::connectionError(const String& errorMessage)
{
	if (enabled->boolValue()) NLOGERROR(niceName, "Connection error " << errorMessage);
}

void GenericOSCQueryModule::dataReceived(const MemoryBlock& data)
{
	if (logIncomingData->boolValue())
	{
		NLOG(niceName, "Websocket data received : " << (int)data.getSize() << " bytes");
	}

	inActivityTrigger->trigger();

	OSCPacketParser parser(data.getData(), (int)data.getSize());
	OSCMessage m = parser.readMessage();
	if (m.isEmpty())
	{
		LOGERROR("Empty message");
		return;
	}
	OSCHelpers::findControllableAndHandleMessage(&valuesCC, m);
}

void GenericOSCQueryModule::messageReceived(const String& message)
{
	if (logIncomingData->boolValue())
	{
		NLOG(niceName, "Websocket message received : " << message);
	}

	inActivityTrigger->trigger();

}

var GenericOSCQueryModule::getJSONData()
{
	var data = Module::getJSONData();
	data.getDynamicObject()->setProperty("treeData", treeData);
	return data;
}

void GenericOSCQueryModule::loadJSONDataInternal(var data)
{
	createTreeFromData(data.getProperty("treeData",var()));
	Module::loadJSONDataInternal(data);
}

void GenericOSCQueryModule::afterLoadJSONDataInternal()
{
	Module::afterLoadJSONDataInternal();
	syncData();
}

void GenericOSCQueryModule::run()
{
	if (useLocal == nullptr || remoteHost == nullptr || remotePort == nullptr) return;

	sleep(100); //safety

	requestHostInfo();
	requestStructure();

}

void GenericOSCQueryModule::requestHostInfo()
{
	URL url("http://" + (useLocal->boolValue() ? "127.0.0.1" : remoteHost->stringValue()) + ":" + String(remotePort->intValue())+"?HOST_INFO");
	StringPairArray responseHeaders;
	int statusCode = 0;
	std::unique_ptr<InputStream> stream(url.createInputStream(false, nullptr, nullptr, String(),
		2000, // timeout in millisecs
		&responseHeaders, &statusCode));
#if JUCE_WINDOWS
	if (statusCode != 200)
	{
		NLOGWARNING(niceName, "Failed to request HOST_INFO, status code = " + String(statusCode));
		return;
	}
#endif

	if (stream != nullptr)
	{
		String content = stream->readEntireStreamAsString();
		if (logIncomingData->boolValue()) NLOG(niceName, "Request status code : " << statusCode << ", content :\n" << content);

		inActivityTrigger->trigger();

		var data = JSON::parse(content);
		if (data.isObject())
		{
			if (logIncomingData->boolValue()) NLOG(niceName, "Received HOST_INFO :\n" << JSON::toString(data));

			int oscPort = data.getProperty("OSC_PORT", remotePort->intValue());
			if (oscPort != remotePort->intValue())
			{
				NLOG(niceName, "OSC_PORT is different from remotePort, setting custom OSC Port to " << oscPort);
				remoteOSCPort->setEnabled(true);
				remoteOSCPort->setValue(oscPort);
			}

			hasListenExtension =  data.getProperty("EXTENSIONS",var()).getProperty("LISTEN", false);
			setupWSClient();
		}
	}
	else
	{
		if (logIncomingData->boolValue()) NLOGWARNING(niceName, "Error with host info request, status code : " << statusCode << ", url : " << url.toString(true));
	}
}

void GenericOSCQueryModule::requestStructure()
{
	URL url("http://" + (useLocal->boolValue() ? "127.0.0.1" : remoteHost->stringValue()) + ":" + String(remotePort->intValue()));
	StringPairArray responseHeaders;
	int statusCode = 0;
	std::unique_ptr<InputStream> stream(url.createInputStream(false, nullptr, nullptr, String(),
		2000, // timeout in millisecs
		&responseHeaders, &statusCode));
#if JUCE_WINDOWS
	if (statusCode != 200)
	{
		NLOGWARNING(niceName, "Failed to request Structure, status code = " + String(statusCode));
		return;
	}
#endif


	if (stream != nullptr)
	{
		String content = stream->readEntireStreamAsString();
		if (logIncomingData->boolValue()) NLOG(niceName, "Request status code : " << statusCode << ", content :\n" << content);

		inActivityTrigger->trigger();

		var data = JSON::parse(content);
		if (data.isObject())
		{
			//if (logIncomingData->boolValue()) NLOG(niceName, "Received structure :\n" << JSON::toString(data));

			createTreeFromData(data);

			Array<var> args;
			args.add(data);
			scriptManager->callFunctionOnAllItems(dataStructureEventId, args);
		}
	}
	else
	{
		if (logIncomingData->boolValue()) NLOGWARNING(niceName, "Error with request, status code : " << statusCode << ", url : " << url.toString(true));
	}
}

void GenericOSCQueryModule::handleRoutedModuleValue(Controllable* c, RouteParams* p)
{

	if (Parameter* sourceP = dynamic_cast<Parameter*>(c))
	{
		OSCQueryRouteParams* qp = (OSCQueryRouteParams*)p;
		if (qp->cRef.wasObjectDeleted() || qp->cRef == nullptr) return;

		if (Parameter* outP = dynamic_cast<Parameter*>(qp->cRef.get()))
		{
			if (outP->value.isArray() == sourceP->value.isArray())
			{
				outP->setValue(sourceP->value);
			}
		}
	}
}


OSCQueryOutput::OSCQueryOutput(GenericOSCQueryModule* module) :
	EnablingControllableContainer("Output"),
	module(module)
{

}

OSCQueryOutput::~OSCQueryOutput()
{
}

InspectableEditor* OSCQueryOutput::getEditor(bool isRoot)
{
	return new OSCQueryModuleOutputEditor(this, isRoot);
}

GenericOSCQueryModule::OSCQueryRouteParams::OSCQueryRouteParams(GenericOSCQueryModule* outModule, Module* sourceModule, Controllable* c)
{
	target = addTargetParameter("Target", "The target value to modify", &outModule->valuesCC);
	target->showTriggers = false;
}

GenericOSCQueryModule::OSCQueryRouteParams::~OSCQueryRouteParams()
{
	setControllable(nullptr);
}

void GenericOSCQueryModule::OSCQueryRouteParams::setControllable(Controllable* c)
{
	if (!cRef.wasObjectDeleted() && cRef != nullptr)
	{
		cRef->removeInspectableListener(this);
	}

	cRef = c;

	if (cRef != nullptr)
	{
		cRef->addInspectableListener(this);
	}
}

void GenericOSCQueryModule::OSCQueryRouteParams::onContainerParameterChanged(Parameter* p)
{
	if (p == target) setControllable(target->target);
}

void GenericOSCQueryModule::OSCQueryRouteParams::inspectableDestroyed(Inspectable* i)
{
	if (i == cRef) setControllable(nullptr);
}

GenericOSCQueryValueContainer::GenericOSCQueryValueContainer(const String &name) :
	ControllableContainer(name)
{
	enableListen = addBoolParameter("Listen", "This will activate listening to this container", false);
	enableListen->hideInEditor = true;
}

GenericOSCQueryValueContainer::~GenericOSCQueryValueContainer()
{
}

InspectableEditor* GenericOSCQueryValueContainer::getEditor(bool isRoot)
{
	return new GenericOSCQueryValueContainerEditor(this, isRoot);
}
