#include "Precomp.h"
#include "Test.h"
#include "VM/Frame.h"
#include "Packages/Core/UState.h"
#include "Packages/Core/Properties/UIntProperty.h"

TEST(StateFrame_PreservesLogicalStateAndItsLocalStorage)
{
	UState codeOwner("patrol", nullptr, {});
	UState logicalState("stateLeadingActor", nullptr, {});
	UIntProperty local("ChildLocal", nullptr, {});
	logicalState.BaseStruct = &codeOwner;
	logicalState.StructSize = sizeof(int32_t);
	logicalState.StructAlignment = alignof(int32_t);
	logicalState.Properties.push_back(&local);
	UObject actor("Actor", nullptr, {});
	actor.StateFrame = std::make_shared<Frame>(&actor, &codeOwner, 0, true, &logicalState);
	CHECK(actor.GetStateName() == "stateLeadingActor");
	CHECK(actor.StateFrame->GetFunc() == &codeOwner);
	auto value = ExpressionValue::Variable(actor.StateFrame->GetVariables(), &local);
	CHECK_EQ(value.ToInt(), 0);
	value.Store(ExpressionValue::IntValue(73));
	CHECK_EQ(value.ToInt(), 73);
	actor.StateFrame.reset();
}

TEST(StateFrame_SetStateReplacesLogicalStateAndStorage)
{
	UState first("First", nullptr, {});
	UState second("Second", nullptr, {});
	UObject actor("Actor", nullptr, {});
	actor.StateFrame = std::make_shared<Frame>(&actor, &first, 0, true);
	actor.StateFrame->SetState(&second);
	CHECK(actor.GetStateName() == "Second");
	CHECK(actor.StateFrame->GetFunc() == &second);
	actor.StateFrame.reset();
}
