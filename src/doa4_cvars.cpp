// Cvars the port defines. The Escape menu saves them to dead_or_alive_4.toml.

#include <rex/cvar.h>

// Extras tab: game tweaks (doa4_tweaks.h). Applied at load (OnPostLoadXexImage).
REXCVAR_DEFINE_BOOL(doa4_tag_all_stages, false, "DOA4",
                    "All stages in Tag battle (Xenia patch by ryu_highabusa)");
REXCVAR_DEFINE_BOOL(doa4_helena_opera, false, "DOA4",
                    "Helena's opera costume (on the disc, cutscene-only) as her 6th costume");
