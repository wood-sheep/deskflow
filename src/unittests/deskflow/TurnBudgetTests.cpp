/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "TurnBudgetTests.h"

#include "deskflow/TurnBudget.h"

void TurnBudgetTests::startsEmpty()
{
  TurnBudget budget;
  QCOMPARE(budget.messages(), 0U);
  QCOMPARE(budget.bytes(), 0U);
  QVERIFY(!budget.exhausted());
}

void TurnBudgetTests::capsMessageCount()
{
  TurnBudget budget;
  budget.beginTurn();

  for (uint32_t i = 0; i < TurnBudget::kMaxMessages - 1; ++i) {
    QVERIFY(!budget.exhausted());
    budget.consume(1);
  }

  QVERIFY(!budget.consume(1));
  QVERIFY(budget.exhausted());
}

void TurnBudgetTests::capsByteCount()
{
  TurnBudget budget;
  budget.beginTurn();

  QVERIFY(budget.consume(TurnBudget::kMaxBytes - 1));
  QVERIFY(!budget.consume(1));
  QVERIFY(budget.exhausted());
}

void TurnBudgetTests::allowsExactlyTheBudget()
{
  TurnBudget budget;
  budget.beginTurn();

  QVERIFY(!budget.consume(TurnBudget::kMaxBytes));
}

void TurnBudgetTests::resetStartsNewTurn()
{
  TurnBudget budget;
  budget.beginTurn();
  budget.consume(TurnBudget::kMaxBytes);
  QVERIFY(budget.exhausted());

  budget.beginTurn();
  QVERIFY(!budget.exhausted());
  QCOMPARE(budget.messages(), 0U);
  QCOMPARE(budget.bytes(), 0U);
}

QTEST_MAIN(TurnBudgetTests)
