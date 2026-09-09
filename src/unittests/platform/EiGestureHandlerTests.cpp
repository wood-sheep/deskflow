#include "platform/EiGestureHandler.h"

#include <QTest>
#include <linux/input-event-codes.h>
#include <utility>
#include <vector>

class EiGestureHandlerTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void swipeUpTriggersOverviewOnlyAtEnd();
  void swipeDownTriggersDesktop();
  void horizontalSwipeHoldsAndReleasesAlt();
  void leftSwipeStartsInReverse();
  void updatesAreThrottledAndAllowDirectionChanges();
  void cancelReleasesAlt();
  void resetReleasesAltOnce();
  void ignoresUpdatesWithoutBegin();
  void ignoresNonThreeFingerGestures();
};

using KeyEvents = std::vector<std::pair<uint32_t, bool>>;
using deskflow::EiGestureHandler;

void EiGestureHandlerTests::swipeUpTriggersOverviewOnlyAtEnd()
{
  KeyEvents keys;
  EiGestureHandler handler([&](uint32_t key, bool down) { keys.emplace_back(key, down); });
  handler.handle({GestureType::SwipeUp, GesturePhase::Begin, 3});
  handler.handle({GestureType::SwipeUp, GesturePhase::Update, 3});
  QVERIFY(keys.empty());
  handler.handle({GestureType::SwipeUp, GesturePhase::End, 3});
  const KeyEvents expected{{KEY_LEFTMETA, true}, {KEY_W, true}, {KEY_W, false}, {KEY_LEFTMETA, false}};
  QCOMPARE(keys, expected);
}

void EiGestureHandlerTests::swipeDownTriggersDesktop()
{
  KeyEvents keys;
  EiGestureHandler handler([&](uint32_t key, bool down) { keys.emplace_back(key, down); });
  handler.handle({GestureType::SwipeDown, GesturePhase::End, 3});
  const KeyEvents expected{{KEY_LEFTMETA, true}, {KEY_D, true}, {KEY_D, false}, {KEY_LEFTMETA, false}};
  QCOMPARE(keys, expected);
}

void EiGestureHandlerTests::horizontalSwipeHoldsAndReleasesAlt()
{
  KeyEvents keys;
  EiGestureHandler handler([&](uint32_t key, bool down) { keys.emplace_back(key, down); });
  handler.handle({GestureType::SwipeRight, GesturePhase::Begin, 3});
  handler.handle({GestureType::SwipeRight, GesturePhase::Begin, 3});
  const KeyEvents held{{KEY_LEFTALT, true}, {KEY_TAB, true}, {KEY_TAB, false}};
  QCOMPARE(keys, held);
  handler.handle({GestureType::SwipeRight, GesturePhase::End, 3});
  QCOMPARE(keys.back(), std::make_pair(uint32_t(KEY_LEFTALT), false));
  QCOMPARE(keys.size(), size_t(4));
}

void EiGestureHandlerTests::leftSwipeStartsInReverse()
{
  KeyEvents keys;
  EiGestureHandler handler([&](uint32_t key, bool down) { keys.emplace_back(key, down); });
  handler.handle({GestureType::SwipeLeft, GesturePhase::Begin, 3});
  handler.handle({GestureType::SwipeLeft, GesturePhase::End, 3});
  const KeyEvents expected{{KEY_LEFTALT, true}, {KEY_LEFTSHIFT, true},  {KEY_TAB, true},
                           {KEY_TAB, false},    {KEY_LEFTSHIFT, false}, {KEY_LEFTALT, false}};
  QCOMPARE(keys, expected);
}

void EiGestureHandlerTests::updatesAreThrottledAndAllowDirectionChanges()
{
  KeyEvents keys;
  EiGestureHandler handler([&](uint32_t key, bool down) { keys.emplace_back(key, down); });
  const auto start = EiGestureHandler::Clock::now();
  handler.handle({GestureType::SwipeRight, GesturePhase::Begin, 3}, start);
  keys.clear();
  handler.handle({GestureType::SwipeRight, GesturePhase::Update, 3}, start + std::chrono::milliseconds(20));
  QVERIFY(keys.empty());
  handler.handle({GestureType::SwipeRight, GesturePhase::Update, 3}, start + std::chrono::milliseconds(80));
  handler.handle({GestureType::SwipeLeft, GesturePhase::Update, 3}, start + std::chrono::milliseconds(160));
  const KeyEvents expected{{KEY_RIGHT, true}, {KEY_RIGHT, false}, {KEY_LEFT, true}, {KEY_LEFT, false}};
  QCOMPARE(keys, expected);
  handler.reset();
}

void EiGestureHandlerTests::cancelReleasesAlt()
{
  KeyEvents keys;
  EiGestureHandler handler([&](uint32_t key, bool down) { keys.emplace_back(key, down); });
  handler.handle({GestureType::SwipeRight, GesturePhase::Begin, 3});
  keys.clear();
  handler.handle({GestureType::SwipeUp, GesturePhase::Cancel, 3});
  const KeyEvents expected{{KEY_LEFTALT, false}};
  QCOMPARE(keys, expected);
}

void EiGestureHandlerTests::resetReleasesAltOnce()
{
  KeyEvents keys;
  EiGestureHandler handler([&](uint32_t key, bool down) { keys.emplace_back(key, down); });
  handler.handle({GestureType::SwipeRight, GesturePhase::Begin, 3});
  keys.clear();
  handler.reset();
  handler.reset();
  const KeyEvents expected{{KEY_LEFTALT, false}};
  QCOMPARE(keys, expected);
}

void EiGestureHandlerTests::ignoresUpdatesWithoutBegin()
{
  KeyEvents keys;
  EiGestureHandler handler([&](uint32_t key, bool down) { keys.emplace_back(key, down); });
  handler.handle({GestureType::SwipeRight, GesturePhase::Update, 3});
  handler.handle({GestureType::SwipeRight, GesturePhase::End, 3});
  handler.handle({GestureType::SwipeDown, GesturePhase::Cancel, 3});
  QVERIFY(keys.empty());
}

void EiGestureHandlerTests::ignoresNonThreeFingerGestures()
{
  KeyEvents keys;
  EiGestureHandler handler([&](uint32_t key, bool down) { keys.emplace_back(key, down); });
  handler.handle({GestureType::SwipeDown, GesturePhase::End, 2});
  handler.handle({GestureType::SwipeLeft, GesturePhase::Begin, 4});
  QVERIFY(keys.empty());
}

QTEST_GUILESS_MAIN(EiGestureHandlerTests)
#include "EiGestureHandlerTests.moc"
