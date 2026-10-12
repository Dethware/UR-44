// Ghidra headless post-script: decompile every non-thunk function into one C file (arg 0 = output path).
// Used by re/ghidra/decompile_all.sh.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class DumpAll extends GhidraScript {
  public void run() throws Exception {
    String out = getScriptArgs()[0];
    DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
    try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
      for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
        if (f.isExternal() || f.isThunk()) continue;
        DecompileResults r = d.decompileFunction(f, 60, monitor);
        w.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
        if (r.decompileCompleted()) w.println(r.getDecompiledFunction().getC()); else w.println("// FAILED");
      }
    }
  }
}
