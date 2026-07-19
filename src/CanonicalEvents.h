#ifndef SEELIE_CANONICALEVENTS_H
#define SEELIE_CANONICALEVENTS_H

// Canonical event names exchanged over the IPC protocol and dispatched
// inside the C++ core. Defined as named constexpr constants so consumers
// (TipsEngine, EcgWidget, PetStateMachine, EventRouter validators) can
// `CanonicalEvents::SessionError` instead of a literal "session.error",
// turning the most common typo class — `"sesion.error"` — into a
// compile-time error. L13.
//
// We intentionally do NOT use Q_NAMESPACE / Q_ENUM_NS here: the canonical
// names are also the wire-protocol strings that gateways and the JSON IPC
// layer send/receive. Renaming them would break compatibility with the
// gateway CLIs and any user's existing hook configuration. The constexpr
// approach gives the typo benefit without touching the wire format.
namespace CanonicalEvents {

inline constexpr const char *SessionStart        = "session.start";
inline constexpr const char *SessionEnd          = "session.end";
inline constexpr const char *SessionIdle         = "session.idle";
inline constexpr const char *SessionError        = "session.error";

inline constexpr const char *PromptSubmitted     = "prompt.submitted";

inline constexpr const char *ToolBefore          = "tool.before";
inline constexpr const char *ToolAfter           = "tool.after";
inline constexpr const char *ToolFailed          = "tool.failed";

inline constexpr const char *PermissionRequested = "permission.requested";
inline constexpr const char *PermissionDenied    = "permission.denied";
inline constexpr const char *PermissionResponse  = "permission.response";

inline constexpr const char *SubagentStarted     = "subagent.started";
inline constexpr const char *SubagentStopped     = "subagent.stopped";

inline constexpr const char *NotificationSent    = "notification.sent";

inline constexpr const char *FileEdited          = "file.edited";
inline constexpr const char *FileWatched         = "file.watched";

inline constexpr const char *TodoUpdated         = "todo.updated";

// ContextSenses (Spec 2): synthetic events emitted by SystemContextEngine.
// source "system" needs no s_validSources entry — unknown sources are
// accepted with a qDebug, unknown events are rejected by EventRouter.
inline constexpr const char *ContextLateNight     = "context.latenight";
inline constexpr const char *ContextLongSession   = "context.longsession";
inline constexpr const char *ContextIdle          = "context.idle";
inline constexpr const char *ContextAway          = "context.away";
inline constexpr const char *ContextGaming        = "context.gaming";
inline constexpr const char *ContextLowBattery    = "context.lowbattery";
inline constexpr const char *ContextTimeOfDay     = "context.timeofday";

// Mood engine proactive events (Spec: 2026-07-19 mood-engine).
inline constexpr const char *MoodGreeting    = "mood.greeting";
inline constexpr const char *MoodLongSession = "mood.long_session";
inline constexpr const char *MoodMissedYou   = "mood.missed_you";
inline constexpr const char *MoodStageUp     = "mood.stage_up";

} // namespace CanonicalEvents

#endif // SEELIE_CANONICALEVENTS_H
