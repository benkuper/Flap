/*
  ==============================================================================

    StateViewUI.h
    Created: 28 Oct 2016 8:27:57pm
    Author:  bkupe

  ==============================================================================
*/

#ifndef STATEVIEWUI_H_INCLUDED
#define STATEVIEWUI_H_INCLUDED

#include "BaseItemUI.h"
#include "State.h"
#include "ActionManagerUI.h"
#include "MappingManagerUI.h"


class StateViewUI :
	public BaseItemUI<State>,
	public ActionManagerUI::ManagerUIListener,
	public MappingManagerUI::ManagerUIListener
{
public:
	StateViewUI(State * state);
	~StateViewUI();

	ScopedPointer<BoolToggleUI> activeUI;
	ScopedPointer<BoolToggleUI> permanentUI;

	ActionManagerUI amui;
	MappingManagerUI mmui;

	ResizableCornerComponent resizer;
	const int grabberHeight = 10;

	//layout
	Component contentContainer;

	//interaction
	Point<float> posAtMouseDown;

	//transition
	bool transitionReceptionMode;
	void setTransitionReceptionMode(bool value);

	void updateMiniModeUI();

	void mouseDown(const MouseEvent &e) override;
	void mouseDrag(const MouseEvent &e) override;
	void mouseDoubleClick(const MouseEvent &e) override;

	void paintOverChildren(Graphics &g) override;

	void resized() override;
	void childBoundsChanged(Component *) override;
	void controllableFeedbackUpdateInternal(Controllable *) override;

	void inspectableSelectionChanged(Inspectable *) override;

	class Grabber : public Component
	{
	public:
		Grabber() {}
		~Grabber() {}
		void paint(Graphics &g) override;
		
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Grabber)
	};
	Grabber grabber;

	
	class Listener
	{
	public:
		virtual ~Listener() {}
		virtual void editorGrabbed(StateViewUI *) {}
		virtual void editorMiniModeChanged(StateViewUI *) {}
		virtual void editorSelectionChanged(StateViewUI *) {}
		virtual void askCreateTransitionFromUI(StateViewUI *) {}
		virtual void askFinishTransitionFromUI(StateViewUI *) {}
	};

	ListenerList<Listener> stateEditorListeners;
	void addStateViewUIListener(Listener* newListener) { stateEditorListeners.add(newListener); }
	void removeStateViewUIListener(Listener* listener) { stateEditorListeners.remove(listener); }

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StateViewUI)
};

#endif  // STATEVIEWUI_H_INCLUDED
