# Script events raised by HP1 native code

Every `ENGINE_<Event>` FName in HP1 Engine.dll and the native functions that raise it (inline `eventX`
wrappers expanded to their callers; `UEngine::Init` only registers the names). "SE" = whether SurrealEngine
(`engine/` + `hp1/`) raises the event anywhere (`CallEvent` with `EventName::X` or the name as a string).
A missing raise is the same kind of gap as `Mount` was: the script side exists, the engine never calls it.
It only says the event is raised somewhere; each call site still has to be compared by hand.

Generated with `tools/ida_dump.py` (`../ida/hp1/decomp/Engine_script_events.json`). Decompiled bodies of every
function: `../ida/hp1/decomp/<Dll>/<ADDR>_<name>.c`, index in `../ida/hp1/decomp/<Dll>_index.tsv`.

## Not raised by SurrealEngine

| Event | Raised in HP1 by |
|---|---|
| BroadcastMessage | UGameEngine::Exec `0x10399580` |
| ClientHearSound | AActor::CheckHearSound `0x1040AB90` |
| ClientMessage | UClient::Exec `0x10386CF0`, ULevel::TickNetServer `0x103B60E0` |
| DemoPlaySound | AActor::execPlaySound `0x1040B000`, AActor::execPlayOwnedSound `0x1040B890` |
| FinishedInterpolation | AInterpolationManager::performPhysics `0x103F7BA0` |
| LogGameSpecial | AStatLog::execInitialCheck `0x10309670`, AStatLog::execLogMutator `0x1030A560` |
| LogGameSpecial2 | AStatLog::execInitialCheck `0x10309670` |
| PostNetBeginPlay | UActorChannel::ReceivedBunch `0x10435E70` |
| ServerTravel | UGameEngine::Exec `0x10399580` |
| ShowUpgradeMenu | UGameEngine::SetProgress `0x10399350` |
| UpdateCamera | AInterpolationManager::performPhysics `0x103F7BA0` |

`Falling` is raised from walking and rolling (`kw/KWPawn.cpp` `StartFalling`); the physSpider/findNewFloor call sites
aren't, since no HP1 script uses PHYS_Spider. FinishedInterpolation/UpdateCamera (and InterpolateEnd with the manager
and bForward) are now raised by the InterpolationManager port (`kw/KWInterpolation.cpp`).

## Raised by both

| Event | Raised in HP1 by |
|---|---|
| AcceptInventory | ULevel::SpawnPlayActor `0x103A8270` |
| ActorEntered | ULevel::SetActorZone `0x103ACDD0` |
| ActorLeaving | ULevel::SetActorZone `0x103ACDD0` |
| AlterDestination | APawn::execPollMoveToward `0x103D89C0`, APawn::execPollStrafeFacing `0x103D9010` |
| AnimEnd | AActor::Tick `0x103B3840` |
| Attach | AActor::SetBase `0x1037A6F0` |
| BaseChange | AActor::SetBase `0x1037A6F0` |
| BeginPlay | UGameEngine::LoadMap `0x1039C3D0`, ULevel::SpawnActor `0x103A65A0` |
| BotDesireability | APawn::breadthPathToInventory `0x10403990` |
| Bump | AActor::PostNetReceive `0x10378980`, ULevel::MoveActor `0x103AA3A0` |
| Destroyed | ULevel::DestroyActor `0x103A72C0` |
| Detach | AActor::SetBase `0x1037A6F0` |
| DetailChange | ULevel::DetailChange `0x103AE630` |
| DoJump | APawn::physWalking `0x103E6B60` |
| EncroachedBy | ULevel::CheckEncroachment `0x103AB5F0` |
| EncroachingOn | ULevel::CheckEncroachment `0x103AB5F0` |
| EndedRotation | AActor::physicsRotation `0x103E5FB0` |
| EnemyNotVisible | APawn::CheckEnemyVisible `0x103DB710` |
| Expired | AActor::Tick `0x103B3840` |
| Falling | APawn::physWalking `0x103E6B60`, AActor::physRolling `0x103F3040`, APawn::findNewFloor `0x103F43E0`, APawn::physSpider `0x103F4A00` |
| FellOutOfWorld | APawn::physWalking `0x103E6B60`, AActor::physFalling `0x103EEA20` |
| FootZoneChange | ULevel::SetActorZone `0x103ACDD0` |
| GainedChild | AActor::SetOwner `0x1037A5E0` |
| HeadZoneChange | ULevel::SetActorZone `0x103ACDD0` |
| HearNoise | AActor::CheckNoiseHearing `0x103DAFC0` |
| HitWall | AActor::moveSmooth `0x103E4C30`, AActor::processHitWall `0x103ECF20`, AActor::physFalling `0x103EEA20`, AActor::physProjectile `0x103F2AB0`, AActor::physRolling `0x103F3040`, APawn::physSpider `0x103F4A00` |
| InitGame | UGameEngine::LoadMap `0x1039C3D0` |
| InterpolateEnd | AInterpolationManager::performPhysics `0x103F7BA0` |
| KeyFrameReached | AActor::physMovingBrush `0x104061F0` |
| Landed | AActor::processLanded `0x103ED210` |
| Login | ULevel::SpawnPlayActor `0x103A8270` |
| LongFall | APawn::execPollWaitForLanding `0x103D6EF0` |
| LostChild | AActor::SetOwner `0x1037A5E0`, ULevel::DestroyActor `0x103A72C0` |
| MayFall | APawn::physWalking `0x103E6B60` |
| Mount | APawn::Mount `0x103EBFB0` |
| PainTimer | AActor::Tick `0x103B3840` |
| PlayerCalcView | UGameEngine::Draw `0x1039FA40`, ULevel::ServerTickClient `0x103B5100`, APlayerPawn::execScreenToWorld `0x103D5AC0` |
| PlayerInput | AActor::Tick `0x103B3840`, ULevel::Tick `0x103B6DB0` |
| PlayerTick | AActor::Tick `0x103B3840` |
| Possess | APlayerPawn::SetPlayer `0x10379530` |
| PostBeginPlay | UGameEngine::LoadMap `0x1039C3D0`, ULevel::SpawnActor `0x103A65A0` |
| PostLogin | ULevel::SpawnPlayActor `0x103A8270` |
| PostRender | UGameEngine::Draw `0x1039FA40` |
| PostTouch | AActor::performPhysics `0x103E52C0`, APawn::performPhysics `0x103E5520` |
| PreBeginPlay | UGameEngine::LoadMap `0x1039C3D0`, ULevel::SpawnActor `0x103A65A0` |
| PreClientTravel | APlayerPawn::execClientTravel `0x104071F0` |
| PreLogin | ULevel::NotifyReceivedText `0x103B1030` |
| PreRender | UGameEngine::Draw `0x1039FA40` |
| RenderTexture | UScriptedTexture::Tick `0x10417C70` |
| SeePlayer | APawn::ShowSelf `0x103DB880` |
| SetInitialState | UGameEngine::LoadMap `0x1039C3D0`, ULevel::SpawnActor `0x103A65A0` |
| Spawned | ULevel::SpawnActor `0x103A65A0` |
| SpawnNotification | ULevel::SpawnActor `0x103A65A0` |
| SpecialCost | APawn::clearPath `0x104005F0`, APawn::clearPaths `0x104006B0`, sub_10400870 `0x10400870` |
| SpecialHandling | APawn::HandleSpecial `0x103E1460` |
| SpeechTimer | AActor::Tick `0x103B3840` |
| Tick | UConsole::PreRender `0x1038BB00`, AActor::Tick `0x103B3840` |
| Timer | AActor::Tick `0x103B3840` |
| Touch | sub_1037A0A0 `0x1037A0A0`, APawn::moveToward `0x103D96F0` |
| TravelPostAccept | ULevel::SpawnPlayActor `0x103A8270` |
| TravelPreAccept | ULevel::SpawnPlayActor `0x103A8270` |
| UnTouch | AActor::EndTouch `0x1037A3E0` |
| UpdateEyeHeight | AActor::Tick `0x103B3840` |
| UpdateTactics | AActor::Tick `0x103B3840` |
| ViewFlash | UGameEngine::Tick `0x103A0900` |
| ZoneChange | ULevel::SetActorZone `0x103ACDD0` |

## No native caller in HP1

Accept, BeginEvent, BroadcastLocalizedMessage, ClientTravel, EndEvent, ForceGenerate, GameEnding, Generate, GetBeaconText, KillCredit, KilledBy, PlayerTimeOut, PostTeleport, PreTeleport, ReceiveLocalizedMessage, RenderOverlays, TakeDamage, TeamMessage, Trigger, UnPossess, UnTrigger, Update, WalkTexture, targeted
