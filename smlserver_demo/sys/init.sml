val _ = Ns.log (Ns.Notice, "executing init.sml...")
val _ = Ns.registerTrap "/demo/trap.txt"
(*val _ = Ns.scheduleScript "/demo/log_time.sml" 5*)
val _ = Ns.log (Ns.Notice, "...done executing init.sml")
