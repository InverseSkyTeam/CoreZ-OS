from __future__ import annotations

import argparse
import io
import os
import shlex
import shutil
import subprocess
import sys
import time
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Iterator, List, Optional, Sequence, Tuple

def _force_utf8_stdout() -> None:
    for stream_name in ("stdout", "stderr"):
        stream = getattr(sys, stream_name, None)
        if stream is None:
            continue
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, io.UnsupportedOperation):
            try:
                setattr(sys, stream_name,
                        io.TextIOWrapper(stream.buffer, encoding="utf-8",
                                         errors="replace"))
            except Exception:
                pass

_force_utf8_stdout()

ROOT       = Path(__file__).resolve().parent

# Some hosted environments (e.g. sandboxes) make the default TMP/TEMP
# directory read-only. Pick a project-local one up-front so zig's tmp
# files land in a writable spot.
_local_tmp = (ROOT / ".zig-tmp").resolve()
os.makedirs(_local_tmp, exist_ok=True)
os.environ["TMP"] = os.environ["TEMP"] = str(_local_tmp)
os.environ.setdefault("ZIG_GLOBAL_CACHE_DIR", str(ROOT / ".zig-cache"))
SRC_DIR    = ROOT / "src"
BOOT_DIR   = SRC_DIR / "boot"
CMD_DIR    = SRC_DIR / "command"
KERNEL_DIR = SRC_DIR / "kernel"
BUILD_DIR  = ROOT / "build"
LINKER_DIR = ROOT / "linker"
SCRIPTS    = ROOT / "scripts"

KERNEL_HDR_GLOBS = [
    "include/*.h", "include/asm/*.h",
    "lib/*/*.h", "lib/user/*.h",
    "memory/*/*.h",
    "thread/*.h",
    "device/*.h",
    "fs/*.h",
    "userprog/*.h",
    "syscall/*.h",
    "shell/*.h",
    "initer/*/*.h",
]

CFLAGS_BASE = ["-ffreestanding", "-fno-builtin", "-fno-sanitize=all",
               "-target", "x86-freestanding",
               "-fmodules-cache-path=" + str(ROOT / ".zig-cache" / "modules")]
UP_CFLAGS_BASE = CFLAGS_BASE + [
    "-I", str(KERNEL_DIR / "lib" / "user"),
    "-I", str(KERNEL_DIR / "lib" / "str"),
    "-I", str(KERNEL_DIR / "lib"),
]

CFLAGS  = CFLAGS_BASE
UP_CFLAGS = UP_CFLAGS_BASE
CFLAGS_OS = CFLAGS                     
CFLAGS_UP = UP_CFLAGS                  
CFLAGS_FONT = UP_CFLAGS + ["-Os"]      
UP_LDFLAGS = ["-s", "-m", "elf_i386", "-Ttext", "0x8048000", "-e", "_start"]

CFLAGS_OS = CFLAGS                     
CFLAGS_UP = UP_CFLAGS                  
CFLAGS_FONT = UP_CFLAGS + ["-Os"]      

class Ansi:
    RESET   = "\x1b[0m"
    BOLD    = "\x1b[1m"
    DIM     = "\x1b[2m"
    ITALIC  = "\x1b[3m"
    UNDER   = "\x1b[4m"
    REV     = "\x1b[7m"
    RED     = "\x1b[31m"
    GREEN   = "\x1b[32m"
    YELLOW  = "\x1b[33m"
    BLUE    = "\x1b[34m"
    MAGENTA = "\x1b[35m"
    CYAN    = "\x1b[36m"
    GRAY    = "\x1b[90m"
    BR_RED  = "\x1b[91m"
    BR_GRN  = "\x1b[92m"
    BR_YEL  = "\x1b[93m"
    BR_BLU  = "\x1b[94m"
    BR_MAG  = "\x1b[95m"
    BR_CYN  = "\x1b[96m"
    BR_WHT  = "\x1b[97m"

    CURSOR_HIDE = "\x1b[?25l"
    CURSOR_SHOW = "\x1b[?25h"
    ERASE_LINE  = "\x1b[2K"
    CR          = "\r"

def _enable_vt_on_windows() -> None:
    
    if os.name != "nt":
        return
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        
        for handle_id in (-11, -12):  
            handle = kernel32.GetStdHandle(handle_id)
            mode = ctypes.c_uint32()
            if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
                kernel32.SetConsoleMode(handle, mode.value | 0x4)
    except Exception:
        pass

def color_enabled(no_color_flag: bool, stream=sys.stdout) -> bool:
    if no_color_flag:
        return False
    if not hasattr(stream, "isatty") or not stream.isatty():
        
        return True
    return True

@dataclass
class Tools:
    nasm:    str
    cc:      List[str]   
    ld:      str
    objcopy: str
    python:  str

def _find(name: str, candidates: Sequence[str]) -> str:
    for c in candidates:
        path = shutil.which(c)
        if path is None:
            continue
        if os.name == "nt":
            base, ext = os.path.splitext(path)
            if ext.lower() != ".exe":
                exe_path = base + ".exe"
                if os.path.exists(exe_path):
                    path = exe_path
        return path
    raise FileNotFoundError(
        f"Could not find any of: {', '.join(candidates)} (needed for `{name}`).\n"
        f"Install the toolchain or add it to PATH."
    )

def _resolve_cc() -> List[str]:
    zig = shutil.which("zig")
    if zig:
        return [zig, "cc"]
    for c in ("cc", "gcc", "x86_64-elf-gcc", "i686-elf-gcc"):
        path = shutil.which(c)
        if path:
            return [path]
    raise FileNotFoundError(
        "Could not find any C compiler (zig/cc/gcc/x86_64-elf-gcc).\n"
        "Install zig 0.13+ or a freestanding-capable GCC."
    )

def detect_tools() -> Tools:
    return Tools(
        nasm    = _find("nasm",    ["nasm"]),
        cc      = _resolve_cc(),                
        ld      = _find("ld",      ["ld.lld", "lld-link", "x86_64-elf-ld", "ld"]),
        objcopy = _find("objcopy", ["objcopy", "llvm-objcopy", "x86_64-elf-objcopy"]),
        python  = sys.executable,
    )

@dataclass
class CmdResult:
    returncode: int
    stdout: str
    stderr: str
    duration: float

    @property
    def ok(self) -> bool:
        return self.returncode == 0

def run(cmd: Sequence[str], cwd: Optional[Path] = None,
        env: Optional[dict] = None, quiet: bool = False) -> CmdResult:
    
    start = time.perf_counter()
    proc = subprocess.run(
        list(cmd),
        cwd=str(cwd) if cwd else None,
        env={**os.environ, **(env or {})},
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    dur = time.perf_counter() - start
    return CmdResult(proc.returncode, proc.stdout, proc.stderr, dur)

class Console:
    def __init__(self, use_color: bool):
        self.use_color = use_color

    def _c(self, code: str) -> str:
        return code if self.use_color else ""

    def write(self, s: str) -> None:
        sys.stdout.write(s)
        sys.stdout.flush()

    def writeln(self, s: str = "") -> None:
        self.write(s + "\n")

    def banner(self, version: str) -> None:
        c = self
        bar = "═" * 70
        c.writeln()
        c.writeln(f"{c._c(Ansi.CYAN)}{c._c(Ansi.BOLD)}{bar}{c._c(Ansi.RESET)}")
        c.writeln(f"{c._c(Ansi.BR_CYN)}{c._c(Ansi.BOLD)}  NiTian OS  "
                  f"{c._c(Ansi.DIM)}{c._c(Ansi.GRAY)}build system  "
                  f"{c._c(Ansi.RESET)}{c._c(Ansi.DIM)}·  {version}{c._c(Ansi.RESET)}")
        c.writeln(f"{c._c(Ansi.CYAN)}{c._c(Ansi.BOLD)}{bar}{c._c(Ansi.RESET)}")
        c.writeln()

    def step_header(self, idx: int, total: int, title: str, hint: str = "") -> None:
        c = self
        bullet = f"{c._c(Ansi.BR_YEL)}▶{c._c(Ansi.RESET)}"
        idx_str = f"{c._c(Ansi.GRAY)}[{idx}/{total}]{c._c(Ansi.RESET)}"
        title_str = f"{c._c(Ansi.BOLD)}{c._c(Ansi.BR_WHT)}{title}{c._c(Ansi.RESET)}"
        hint_str = (f"  {c._c(Ansi.DIM)}{c._c(Ansi.GRAY)}{hint}{c._c(Ansi.RESET)}"
                    if hint else "")
        c.writeln(f"  {bullet} {idx_str}  {title_str}{hint_str}")
        c.writeln(f"  {c._c(Ansi.GRAY)}{'─' * 66}{c._c(Ansi.RESET)}")

    def ok(self, msg: str) -> None:
        self.writeln(f"      {self._c(Ansi.GREEN)}✓{self._c(Ansi.RESET)}  {msg}")

    def info(self, msg: str) -> None:
        self.writeln(f"      {self._c(Ansi.CYAN)}·{self._c(Ansi.RESET)}  {msg}")

    def warn(self, msg: str) -> None:
        self.writeln(f"      {self._c(Ansi.YELLOW)}!{self._c(Ansi.RESET)}  {msg}")

    def fail(self, msg: str) -> None:
        self.writeln(f"      {self._c(Ansi.RED)}✗{self._c(Ansi.RESET)}  {msg}")
    
    @contextmanager
    def progress(self, total: int, label: str, color: str = Ansi.BR_CYN):
        c = self
        width = 38
        started = time.perf_counter()
        state = {"done": 0, "msg": ""}

        def render() -> None:
            pct = (state["done"] / total) if total else 1.0
            fill = int(round(width * pct))
            bar = "█" * fill + "░" * (width - fill)
            elapsed = time.perf_counter() - started
            eta = (elapsed / state["done"]) * (total - state["done"]) if state["done"] else 0.0
            tail = state["msg"][:40]
            sys.stdout.write(
                f"\r      {c._c(color)}▐{bar}▌{c._c(Ansi.RESET)} "
                f"{c._c(Ansi.BOLD)}{pct*100:5.1f}%{c._c(Ansi.RESET)} "
                f"{c._c(Ansi.GRAY)}{state['done']:>3}/{total:<3} "
                f"{c._c(Ansi.DIM)}{tail:<40} "
                f"{c._c(Ansi.GRAY)}elap {elapsed:5.1f}s eta {eta:4.1f}s{c._c(Ansi.RESET)}"
            )
            sys.stdout.flush()

        def update(done: int, msg: str = "") -> None:
            state["done"] = done
            state["msg"] = msg
            render()

        c.write(c._c(Ansi.CURSOR_HIDE))
        try:
            update(0, label)
            yield update
        finally:
            update(total, label)
            sys.stdout.write("\n")
            sys.stdout.flush()
            c.write(c._c(Ansi.CURSOR_SHOW))

    def summary(self, rows: Sequence[Tuple[str, str, str]]) -> None:
        
        c = self
        c.writeln()
        c.writeln(f"  {c._c(Ansi.BOLD)}{c._c(Ansi.BR_WHT)}Summary{c._c(Ansi.RESET)}")
        c.writeln(f"  {c._c(Ansi.GRAY)}{'─' * 66}{c._c(Ansi.RESET)}")
        for label, value, color in rows:
            c.writeln(f"      {c._c(Ansi.GRAY)}{label:<14}{c._c(Ansi.RESET)}"
                      f" {c._c(color)}{c._c(Ansi.BOLD)}{value}{c._c(Ansi.RESET)}")
        c.writeln()

@dataclass
class Task:
    name: str
    cmd:  List[str]
    cwd:  Optional[Path] = None
    out:  Optional[Path] = None       
    deps: List["Task"] = field(default_factory=list)
    description: str = ""
    group: str = ""                   

    def is_stale(self) -> bool:
        if not self.out:
            return True
        if not self.out.exists():
            return True
        out_mtime = self.out.stat().st_mtime
        for d in self.deps:
            if d.is_stale():
                return True
            for path in d.dep_paths():
                if path.exists() and path.stat().st_mtime > out_mtime:
                    return True
        return False

    def dep_paths(self) -> Iterable[Path]:
        for tok in self.cmd:
            if tok.startswith("-"):
                continue
            p = Path(tok)
            if p.exists() and p.is_file():
                yield p

@dataclass
class BuildStats:
    compiled: int = 0
    cache_hit: int = 0
    failed:   int = 0
    timings: dict = field(default_factory=dict)

def task_assemble_bin(name: str, src: Path, out: Path, tools: Tools) -> Task:
    return Task(
        name=name, cmd=[tools.nasm, "-f", "bin", str(src), "-o", str(out)],
        out=out, deps=[], description=str(src.relative_to(ROOT)),
        group="asm",
    )

def task_assemble_elf(name: str, src: Path, out: Path, tools: Tools) -> Task:
    return Task(
        name=name, cmd=[tools.nasm, "-f", "elf32", str(src), "-o", str(out)],
        out=out, deps=[], description=str(src.relative_to(ROOT)),
        group="asm",
    )

def task_cc(name: str, src: Path, out: Path, tools: Tools, flags: List[str]) -> Task:
    return Task(
        name=name,
        cmd=[*tools.cc, "-c", str(src), *flags, "-o", str(out)],
        out=out, deps=[], description=str(src.relative_to(ROOT)),
        group="cc",
    )

def task_link(name: str, out: Path, tools: Tools,
              objs: Sequence[Path], script: Optional[Path] = None,
              flags: Sequence[str] = ()) -> Task:
    cmd = [tools.ld]
    if script:
        cmd += ["-T", str(script)]
    cmd += [*flags, "-o", str(out), *map(str, objs)]
    return Task(name=name, cmd=cmd, cwd=BUILD_DIR, out=out,
                deps=[], description=f"link → {out.name}", group="link")

def task_objcopy_binary(name: str, src_elf: Path, out: Path, tools: Tools,
                        symbol: str) -> Task:
    return Task(
        name=name,
        cmd=[tools.objcopy, "-I", "binary", "-O", "elf32-i386", "-B", "i386",
             src_elf.name, out.name],
        cwd=BUILD_DIR, out=out, deps=[],
        description=f"objcopy {symbol}", group="objcopy",
    )

def task_python(name: str, script: Path, args: Sequence[str],
                out: Optional[Path] = None) -> Task:
    return Task(
        name=name,
        cmd=[sys.executable, str(script), *args],
        out=out, deps=[], description=str(script.name), group="python",
    )

@dataclass
class BuildPlan:
    
    tasks: List[Task]
    user_elves: List[Tuple[Task, Task]]   

    def all(self) -> List[Task]:
        out = list(self.tasks)
        for elf, data in self.user_elves:
            out += [elf, data]
        return out

def make_plan(tools: Tools) -> BuildPlan:
    tasks: List[Task] = []
    user_elves: List[Tuple[Task, Task]] = []

    tasks += [
        task_assemble_bin("boot.bin",
                          BOOT_DIR / "boot.asm", BUILD_DIR / "boot.bin", tools),
        task_assemble_bin("loader.bin",
                          BOOT_DIR / "loader.asm", BUILD_DIR / "loader.bin", tools),
    ]

    for stem in ("func", "io", "stub", "entry", "switch"):
        tasks.append(task_assemble_elf(
            f"{stem}.o",
            KERNEL_DIR / "asmCall" / f"{stem}.asm",
            BUILD_DIR / f"{stem}.o", tools,
        ))

    tasks.append(task_assemble_elf(
        "up_start.o", CMD_DIR / "start.asm", BUILD_DIR / "up_start.o", tools,
    ))

    KERNEL_C_SOURCES = [
        ("ioc.o",        KERNEL_DIR / "initer" / "io" / "io.c"),
        ("pic.o",        KERNEL_DIR / "initer" / "pic" / "pic.c"),
        ("pit.o",        KERNEL_DIR / "initer" / "pit" / "pit.c"),
        ("idt.o",        KERNEL_DIR / "initer" / "idt" / "idt.c"),
        ("interrupt.o",  KERNEL_DIR / "initer" / "idt" / "interrupt.c"),
        ("kernel.o",     KERNEL_DIR / "main.c"),
        ("assert.o",     KERNEL_DIR / "assert.c"),
        ("str.o",        KERNEL_DIR / "lib" / "str" / "str.c"),
        ("bitmap.o",     KERNEL_DIR / "memory" / "bitmap" / "bitmap.c"),
        ("pool.o",       KERNEL_DIR / "memory" / "pool" / "pool.c"),
        ("list.o",       KERNEL_DIR / "lib" / "list" / "list.c"),
        ("thread.o",     KERNEL_DIR / "thread" / "thread.c"),
        ("sync.o",       KERNEL_DIR / "thread" / "sync.c"),
        ("ioqueue.o",    KERNEL_DIR / "device" / "ioqueue.c"),
        ("keyboard.o",   KERNEL_DIR / "device" / "keyboard.c"),
        ("ide.o",        KERNEL_DIR / "device" / "ide.c"),
        ("fs.o",         KERNEL_DIR / "fs" / "fs.c"),
        ("inode.o",      KERNEL_DIR / "fs" / "inode.c"),
        ("dir.o",        KERNEL_DIR / "fs" / "dir.c"),
        ("file.o",       KERNEL_DIR / "fs" / "file.c"),
        ("gdt.o",        KERNEL_DIR / "initer" / "gdt" / "gdt.c"),
        ("tss.o",        KERNEL_DIR / "initer" / "tss" / "tss.c"),
        ("process.o",    KERNEL_DIR / "userprog" / "process.c"),
        ("exec.o",       KERNEL_DIR / "userprog" / "exec.c"),
        ("shell.o",      KERNEL_DIR / "shell" / "shell.c"),
        ("buildin_cmd.o",KERNEL_DIR / "shell" / "buildin_cmd.c"),
        ("pipe.o",       KERNEL_DIR / "shell" / "pipe.c"),
        ("ksyscall.o",   KERNEL_DIR / "syscall" / "syscall.c"),
        ("usyscall.o",   KERNEL_DIR / "lib" / "user" / "syscall.c"),
        ("ustdio.o",     KERNEL_DIR / "lib" / "user" / "stdio.c"),
        ("wait_exit.o",  KERNEL_DIR / "userprog" / "wait_exit.c"),
        ("fork.o",       KERNEL_DIR / "userprog" / "fork.c"),
        ("mouse.o",      KERNEL_DIR / "device" / "mouse.c"),
        ("gfx.o",        KERNEL_DIR / "gui" / "gfx.c"),
        ("shm.o",        KERNEL_DIR / "gui" / "shm.c"),
        ("guiserver.o",  KERNEL_DIR / "gui" / "server.c"),
        ("layout.o",     KERNEL_DIR / "gui" / "layout.c"),
        ("wm.o",         KERNEL_DIR / "gui" / "wm.c"),
        ("guiclients.o", KERNEL_DIR / "gui" / "clients.c"),
        ("gui.o",        KERNEL_DIR / "gui" / "gui.c"),
    ]
    for stem, src in KERNEL_C_SOURCES:
        tasks.append(task_cc(stem, src, BUILD_DIR / stem, tools, CFLAGS_OS))

    USER_PROGRAMS = [
        ("prog_no_arg", "prog_no_arg.c", "main",   []),
        ("prog_arg",    "prog_arg.c",    "_start", []),
        ("cat",         "cat.c",         "_start", []),
        ("fork_demo",   "fork_demo.c",   "_start", []),
        ("prog_pipe",   "prog_pipe.c",   "_start", []),
        ("font_demo",   "font_demo.c",   "_start", ["-Os"]),
        ("heap_demo",   "heap_demo.c",   "_start", []),
    ]

    USER_LIB_SOURCES = [
        (KERNEL_DIR / "lib" / "user", "stdio.c",   "up_stdio.o"),
        (KERNEL_DIR / "lib" / "user", "syscall.c", "up_syscall.o"),
        (KERNEL_DIR / "lib" / "str",  "str.c",     "up_str.o"),
        (KERNEL_DIR / "lib" / "user", "stdlib.c",  "up_stdlib.o"),
    ]

    def compile_user_lib(out_dir: Path) -> List[Path]:
        outs = []
        for _dir, fname, oname in USER_LIB_SOURCES:
            src = _dir / fname
            obj = out_dir / oname
            tasks.append(task_cc(oname, src, obj, tools, CFLAGS_UP))
            outs.append(obj)
        return outs

    lib_objs = compile_user_lib(BUILD_DIR)

    for prog_name, src_c, entry_flag, opt_flags in USER_PROGRAMS:
        nick_map = {"prog_no_arg": "up_no_arg", "prog_arg": "up_arg",
                    "cat": "up_cat", "fork_demo": "up_fork",
                    "prog_pipe": "up_pipe", "font_demo": "up_font",
                    "heap_demo": "up_heap"}
        nick = nick_map.get(prog_name, f"up_{prog_name}")
        prog_obj = BUILD_DIR / f"{nick}.o"
        tasks.append(task_cc(nick, CMD_DIR / src_c, prog_obj, tools,
                             CFLAGS_UP + opt_flags))

        elf_flags = list(UP_LDFLAGS)
        if entry_flag:
            elf_flags[elf_flags.index("-e") + 1] = entry_flag
        elf = BUILD_DIR / f"{prog_name}.elf"
        elf_task = task_link(
            f"{prog_name}.elf", elf, tools,
            [BUILD_DIR / "up_start.o", prog_obj, *lib_objs],
            flags=elf_flags,
        )
        tasks.append(elf_task)

        data_o = BUILD_DIR / f"{prog_name}_data.o"
        data_task = task_objcopy_binary(
            f"{prog_name}_data.o", elf, data_o, tools, symbol=prog_name,
        )
        tasks.append(data_task)
        user_elves.append((elf_task, data_task))

    font_subset = BUILD_DIR / "font_subset.ttf"
    tasks.append(task_python(
        "font_subset.ttf",
        SCRIPTS / "make_font_subset.py",
        [str(KERNEL_DIR / "lib" / "assets" / "font.ttf"), str(font_subset)],
        out=font_subset,
    ))
    font_data_o = BUILD_DIR / "font_subset_ttf_data.o"
    tasks.append(task_objcopy_binary(
        "font_subset_ttf_data.o", font_subset, font_data_o, tools,
        symbol="font_subset_ttf",
    ))

    KERNEL_OBJS = [
        "entry.o", "kernel.o", "func.o", "ioc.o", "io.o",
        "pic.o", "pit.o", "stub.o", "idt.o", "interrupt.o",
        "assert.o", "str.o", "bitmap.o", "pool.o", "list.o",
        "switch.o", "thread.o", "sync.o", "ioqueue.o", "keyboard.o",
        "ide.o", "fs.o", "inode.o", "dir.o", "file.o",
        "gdt.o", "tss.o", "process.o", "exec.o", "shell.o",
        "buildin_cmd.o", "ksyscall.o", "usyscall.o", "ustdio.o",
        "wait_exit.o", "fork.o", "pipe.o",
        "mouse.o", "gfx.o", "shm.o", "guiserver.o", "layout.o",
        "wm.o", "guiclients.o", "gui.o",
    ]
    USER_DATA_OBJS = [
        "prog_no_arg_data.o", "prog_arg_data.o", "cat_data.o",
        "fork_demo_data.o", "prog_pipe_data.o", "font_demo_data.o",
        "heap_demo_data.o",
    ]
    KERNEL_LINK_OBJS = (
        [BUILD_DIR / n for n in KERNEL_OBJS]
        + [BUILD_DIR / n for n in USER_DATA_OBJS]
        + [font_data_o]
    )

    kernel_elf = BUILD_DIR / "kernel.elf"
    tasks.append(task_link(
        "kernel.elf", kernel_elf, tools, KERNEL_LINK_OBJS,
        script=LINKER_DIR / "kernel.ld",
    ))
    kernel_bin = BUILD_DIR / "kernel.bin"
    tasks.append(Task(
        name="kernel.bin",
        cmd=[tools.objcopy, "-O", "binary", str(kernel_elf), str(kernel_bin)],
        out=kernel_bin, deps=[], description="strip ELF → flat binary",
        group="objcopy",
    ))
    
    floppy_img = BUILD_DIR / "floppy.img"
    tasks.append(task_python(
        "floppy.img",
        SCRIPTS / "mkfloppy.py",
        [str(BUILD_DIR / "boot.bin"),
         str(BUILD_DIR / "loader.bin"),
         str(BUILD_DIR / "kernel.bin"),
         str(floppy_img)],
        out=floppy_img,
    ))

    return BuildPlan(tasks=tasks, user_elves=user_elves)

def execute_plan(plan: BuildPlan, tools: Tools, console: Console,
                 jobs: int = 1) -> BuildStats:
    stats = BuildStats()
    overall_t0 = time.perf_counter()

    def run_task(task: Task) -> None:
        t0 = time.perf_counter()
        if task.out and task.out.exists():
            out_mtime = task.out.stat().st_mtime
            stale = False
            for dep_path in task.dep_paths():
                if dep_path.exists() and dep_path.stat().st_mtime > out_mtime:
                    stale = True
                    break
            if not stale:
                stats.cache_hit += 1
                stats.timings[task.name] = (time.perf_counter() - t0, "cached")
                return
        try:
            res = run(task.cmd, cwd=task.cwd)
        except FileNotFoundError as exc:
            stats.failed += 1
            console.fail(f"{task.name}: {exc}")
            raise
        if not res.ok:
            stats.failed += 1
            console.fail(f"{task.name} (exit {res.returncode})")
            
            for line in (res.stderr or res.stdout).splitlines()[-30:]:
                console.writeln(f"          {line}")
            raise SystemExit(res.returncode)
        stats.compiled += 1
        stats.timings[task.name] = (time.perf_counter() - t0, "built")

    total_steps = 8
    s = 1

    console.step_header(s, total_steps, "Assembling boot sectors")
    for t in plan.tasks[:2]:
        run_task(t)
        console.ok(f"{t.description}  {c_dim(console, fmt_dur(stats.timings[t.name][0]))}")
    s += 1

    console.step_header(s, total_steps, "Compiling kernel assembly")
    asm_kern = [t for t in plan.tasks if t.group == "asm" and t.name in
                ("func.o", "io.o", "stub.o", "entry.o", "switch.o")]
    for t in asm_kern:
        run_task(t)
        console.ok(f"{t.description}  {c_dim(console, fmt_dur(stats.timings[t.name][0]))}")
    s += 1

    console.step_header(s, total_steps, "Compiling kernel C objects")
    cc_kern = [t for t in plan.tasks if t.group == "cc" and t.name.endswith(".o")
               and not t.name.startswith("up_")]
    with console.progress(len(cc_kern), "kernel C", Ansi.BR_CYN) as update:
        for i, t in enumerate(cc_kern, 1):
            run_task(t)
            update(i, t.description)
    s += 1

    console.step_header(s, total_steps, "Compiling user-program objects")
    user_lib = [t for t in plan.tasks if t.group == "cc" and t.name.startswith("up_")]
    prog_obj = [t for t in plan.tasks if t.group == "cc" and t.name.startswith("up_")
                and t.name not in ("up_start.o",)]
    up_start = [t for t in plan.tasks if t.name == "up_start.o"]
    with console.progress(len(user_lib) + len(prog_obj) + len(up_start),
                          "user C", Ansi.BR_YEL) as update:
        i = 0
        for grp in (up_start, user_lib, prog_obj):
            for t in grp:
                i += 1
                run_task(t)
                update(i, t.description)
    s += 1

    console.step_header(s, total_steps, "Linking & packing user ELFs")
    with console.progress(len(plan.user_elves) * 2 + 1, "user ELFs", Ansi.BR_GRN) as update:
        i = 0
        for elf_task, data_task in plan.user_elves:
            i += 1; run_task(elf_task);  update(i, elf_task.description)
            i += 1; run_task(data_task); update(i, data_task.description)
    s += 1

    console.step_header(s, total_steps, "Generating font subset")
    font_py = [t for t in plan.tasks if t.group == "python" and "font" in t.name]
    font_obj = [t for t in plan.tasks if t.group == "objcopy" and "font" in t.name]
    with console.progress(len(font_py) + len(font_obj), "font", Ansi.BR_MAG) as update:
        i = 0
        for t in font_py + font_obj:
            i += 1; run_task(t); update(i, t.description)
    s += 1

    console.step_header(s, total_steps, "Linking kernel image")
    kernel_link = [t for t in plan.tasks
                   if t.group in ("link", "objcopy") and "kernel" in t.name]
    with console.progress(len(kernel_link), "kernel link", Ansi.BR_CYN) as update:
        for i, t in enumerate(kernel_link, 1):
            run_task(t)
            update(i, t.description)
    s += 1

    console.step_header(s, total_steps, "Packing floppy image")
    floppy_task = [t for t in plan.tasks if t.name == "floppy.img"][0]
    t0 = time.perf_counter()
    res = run(floppy_task.cmd)
    if not res.ok:
        console.fail(f"mkfloppy.py failed (exit {res.returncode})")
        for line in (res.stderr or res.stdout).splitlines()[-20:]:
            console.writeln(f"          {line}")
        raise SystemExit(res.returncode)
    dur = time.perf_counter() - t0
    console.ok(f"build/floppy.img  {c_dim(console, fmt_dur(dur))}")
    s += 1

    stats.timings["__total__"] = (time.perf_counter() - overall_t0, "overall")
    return stats

def fmt_dur(seconds: float) -> str:
    if seconds < 0.05:
        return f"(<0.1s)"
    if seconds < 10:
        return f"({seconds:.2f}s)"
    return f"({seconds/60:.1f}min)"

def c_dim(console: Console, s: str) -> str:
    return f"{console._c(Ansi.DIM)}{console._c(Ansi.GRAY)}{s}{console._c(Ansi.RESET)}"

def show_failure_hint(console: Console, missing: List[str]) -> None:
    console.writeln()
    console.fail("Missing required toolchain components:")
    for m in missing:
        console.writeln(f"          • {m}")
    console.writeln()
    console.info("Install one of the supported toolchains:")
    console.writeln("          • Linux   : apt install nasm zig         OR  apt install nasm lld gcc")
    console.writeln("          • macOS   : brew install nasm zig")
    console.writeln("          • Windows : install zig, nasm, llvm (winget/choco/scoop)")
    console.writeln()

def do_clean(console: Console) -> None:
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
        console.ok(f"removed {BUILD_DIR}")
    else:
        console.info(f"{BUILD_DIR} already absent")

def do_run(console: Console, stats: BuildStats) -> None:
    qemu = shutil.which("qemu-system-i386")
    if qemu is None:
        console.warn("qemu-system-i386 not found on PATH; build is up-to-date.")
        return
    hd_img = BUILD_DIR / "test_hd.img"
    if not hd_img.exists():
        
        mkdisk = SCRIPTS / "make_disk.py"
        if mkdisk.exists():
            console.info("generating test_hd.img via make_disk.py")
            run([sys.executable, str(mkdisk), str(hd_img)])
    console.writeln()
    console.writeln(f"  {console._c(Ansi.BR_GRN)}▶ launching qemu...{console._c(Ansi.RESET)}")
    console.writeln()
    subprocess.run([
        qemu, "-accel", "tcg,tb-size=256", "-m", "1G",
        "-fda", str(BUILD_DIR / "floppy.img"),
        "-hda", str(hd_img),
        "-debugcon", "stdio",
    ])

def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="build.py",
        description="Cross-platform NiTian OS build system",
    )
    parser.add_argument("target", nargs="?", default="floppy",
                        choices=["all", "floppy", "run", "clean"],
                        help="build target (default: floppy)")
    parser.add_argument("--no-color", action="store_true",
                        help="disable ANSI colour output")
    parser.add_argument("--jobs", "-j", type=int, default=1,
                        help="parallel compile jobs (default: 1)")
    parser.add_argument("--version", action="version", version="nitian-build 1.0")
    args = parser.parse_args(argv)

    _enable_vt_on_windows()
    console = Console(use_color=color_enabled(args.no_color))

    console.banner("v1.0  ·  Windows / Linux / macOS")

    if args.target == "clean":
        do_clean(console)
        return 0

    try:
        tools = detect_tools()
    except FileNotFoundError as exc:
        show_failure_hint(console, [str(exc)])
        return 2
    console.info(f"cc      = {tools.cc}")
    console.info(f"ld      = {tools.ld}")
    console.info(f"nasm    = {tools.nasm}")
    console.info(f"objcopy = {tools.objcopy}")
    console.writeln()

    plan = make_plan(tools)
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    try:
        stats = execute_plan(plan, tools, console, jobs=args.jobs)
    except SystemExit as exc:
        return int(exc.code) if exc.code is not None else 1

    floppy = BUILD_DIR / "floppy.img"
    kernel = BUILD_DIR / "kernel.bin"
    elf    = BUILD_DIR / "kernel.elf"

    def human_size(p: Path) -> str:
        try:
            n = p.stat().st_size
        except FileNotFoundError:
            return "—"
        for unit in ("B", "KB", "MB", "GB"):
            if n < 1024 or unit == "GB":
                return f"{n:.1f} {unit}" if unit != "B" else f"{n} {unit}"
            n /= 1024
        return f"{n:.1f} GB"

    total_dur = stats.timings.get("__total__", (0.0, ""))[0]
    console.summary([
        ("artefacts",   f"floppy.img · kernel.bin · kernel.elf", Ansi.BR_WHT),
        ("floppy size", human_size(floppy),                       Ansi.BR_CYN),
        ("kernel size", human_size(kernel),                       Ansi.BR_CYN),
        ("kernel.elf",  human_size(elf),                          Ansi.BR_CYN),
        ("compiled",    str(stats.compiled),                      Ansi.BR_GRN),
        ("cached",      str(stats.cache_hit),                     Ansi.BR_YEL),
        ("failed",      str(stats.failed),                        Ansi.BR_RED if stats.failed else Ansi.GRAY),
        ("total",       fmt_dur(total_dur),                       Ansi.BR_WHT),
    ])
    console.writeln(f"  {console._c(Ansi.GREEN)}{console._c(Ansi.BOLD)}"
                    f"✔  build complete{console._c(Ansi.RESET)}")
    console.writeln()

    if args.target == "run":
        do_run(console, stats)

    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))