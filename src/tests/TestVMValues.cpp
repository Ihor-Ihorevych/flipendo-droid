#include "Precomp.h"
#include "Test.h"
#include "VM/ExpressionValue.h"
#include "VM/ExpressionEvaluator.h"
#include "VM/Expression.h"
#include "Packages/Core/Properties/UIntProperty.h"
#include "Packages/Core/Properties/UStructProperty.h"

TEST(TemporaryRotatorMember_OwnsItsValue)
{
	UIntProperty yaw("Yaw", nullptr, {});
	yaw.DataOffset.DataOffset = offsetof(Rotator, Yaw);
	auto parent = ExpressionValue::RotatorValue(Rotator(11, 12345, 33));
	auto member = parent.Member(&yaw);
	parent = ExpressionValue::RotatorValue(Rotator(44, -6789, 66));
	CHECK_EQ(member.ToInt(), 12345);
}

TEST(VariableRotatorMember_StoresThroughToParent)
{
	UStructProperty rotation("Rotation", nullptr, {});
	UIntProperty yaw("Yaw", nullptr, {});
	yaw.DataOffset.DataOffset = offsetof(Rotator, Yaw);
	Rotator value(11, 22, 33);
	auto parent = ExpressionValue::Variable(&value, &rotation);
	auto member = parent.Member(&yaw);
	member.Store(ExpressionValue::IntValue(54321));
	CHECK_EQ(value.Yaw, 54321);
	CHECK_EQ(value.Pitch, 11);
	CHECK_EQ(value.Roll, 33);
}


TEST(NoneClassContext_SkipsTheContextExpression)
{
	NoObjectExpression none;
	InstanceVariableExpression invalidContext;
	ClassContextExpression expression;
	expression.ObjectExpr = &none;
	expression.ContextExpr = &invalidContext;
	auto result = ExpressionEvaluator::Eval(&expression, nullptr, nullptr, nullptr);
	CHECK(result.Result == StatementResult::AccessedNone);
	CHECK(result.Value.GetType() == ExpressionValueType::Nothing);
}
