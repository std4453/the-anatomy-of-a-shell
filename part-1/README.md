# the anatomy of a shell - part 1

Over the time, new terminals emerge - alacritty, warp, xterm.js - to name a few.

But suprisingly, few new shells give birth. Take the word not from me, but from [wikipedia](https://en.wikipedia.org/wiki/Comparison_of_command_shells): Of the 26 different shells that it felt influential enough to list, only 4 were introduced in the 21st century - only one after 2010.

For junior developers nowadays, shells are something that exist since prehistoric times, and when one talks about _the shell_, one means `bash` , the _Bourne Again SHell_.

Whenever a system wants to let users customize by writing scripts, `bash` that it will use (if, for the sake of rhetoric, we ignore the existance of other popluar scripting languaged like python and lua).

To better comprehend how `bash` is the _de facto_ standard, CI systems run `bash` , Dockerfile run `bash` , and most Linux distributions use it `bash` as default shell.

Being such a basic part for many systems, it is actually a very feature-rich piece of software.

Novices might think that shells are simple: Enter a command, `bash` runs the program.

Some might also know that shells can do parameter escaping, environment variables, wildcards, `&&` and `||` , completion, also input/output redirection and pipes. When `bash` scripts are involved, there’s also control flow primitives like `if` and `for` , `test` , functions, status code and prompts. If that does not sound a lot to you, we have `PATH` , `type` , directory manipulation, arithmetic expressions, variable substitution, aliases, `.bashrc` , history, `trap` , control keys, subshells, arrays, and job control.

`bash` has tons of features and more than 200k lines of code\*, which is normal considering that it’s almost as old as Unix, and still being developed actively till today.

\*: counting GNU Bash

It’s just how neglecting we are to its complexity.

---

So, how does a shell work?

To give readers a structural understanding of this question, we start from the really simple “input command, run command” shell, and then add more features to it.

The one amazing thing about bash is that it evolves along with Unix itself (and Linux, or, more generally speaking, POSIX). As a result, new operating system features empowers the shell, which then help users make a richer ecosystem for the OS.

We’ll see that there’re certain system calls that look as if they were specifically designed for the shell, but more on that later.

Now, let’s get started.

## first, the read-and-run shell

The first prototype of our shell is simple: It reads one line at a time from the input (both standard input and file will do), then executes the line as a command with options, the code is:

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

using namespace std;

struct command {
	string file;
	vector<string> args;
};

command parse_input(const string &input) {
	vector<string> parts;
	// TODO: use a auto-generated parser (e.g. flex & bison) for this
	size_t last_index = 0;
	for (size_t i = 0; i < input.length(); ++i) {
		const auto &ch = input[i];
		if (ch == ' ') {
			parts.emplace_back(input.substr(last_index, i - last_index));
			last_index = i + 1;
		}
	}
	if (last_index < input.length()) {
		parts.emplace_back(input.substr(last_index));
	}
	if (parts.size() == 0) {
		throw "Empty command"s;
	}
	return {
		.file = parts[0],
		.args = { parts.cbegin() + 1, parts.cend() }
	};
}

bool repl_loop(istream &in) {
	cout << "> ";
	string input;
	getline(in, input);
	auto cmd = parse_input(input);
	if (cmd.file == "exit"s) {
		cout << "exiting!" << endl;
		return false;
	}
	// TODO: run command
	cout << "command is:\n\tfile = " << cmd.file << "\n\targs = ";
	for (const auto &arg : cmd.args) {
		cout << arg << " ";
	}
	cout << endl;
	return true;
}

int main() {
	try {
		istream &in = cin;
		while (!in.eof()) {
			if (!repl_loop(in)) {
				break;
			}
		}
	} catch (const string &errstr) {
		cerr << "Fatal error: " << errstr << endl;
	}
}
```

Break it down a bit: Our `main()` function runs `repl_loop()` , which reads a line from whatever input (`stdin` or file), parses the line with `parse_input()` to get command and options, then executes the command. The actual command execution comes a little later, now it just prints the parse result to `cout` . Or, if the command is `exit` , the loop comes to an end.

Compile and run it:

```
$ ./a.out
> a b c
command is:
	file = a
	args = b c
> foo
command is:
	file = foo
	args =
> exit
exiting!
```

Pretty neat, not?

---

Now add command execution, here we don’t want to use [`std::system`](https://en.cppreference.com/w/cpp/utility/program/system) , since that invokes the system shell and loses our point of writing our own. Instead, we need [`exec()`](https://man7.org/linux/man-pages/man3/exec.3.html) from the standard library, which starts processes directly -

Or, does it?

On manpage we see that `exec()` functions **replace the current process**, thus we’ll not be able to return to our shell after the command has completed. Therefore we’ll want first `fork()` a new process before `exec()` -ing.

The `exec()` functions come in two classes: ones with `p` and ones without. Those with `p` accept arbitrary string and tries to find the file according to the current `PATH` environment variable. That is exactly what we need now.

The code should be like (duplicate code omitted):

```cpp
constexpr bool DEBUG = true;

void exec_command(const command &cmd) {
	if (DEBUG) {
		cout << "[" << getpid() << "] command is:\n\tfile = " << cmd.file << "\n\targs = ";
		for (const auto &arg : cmd.args) {
			cout << arg << " ";
		}
		cout << endl;
	}

	pid_t pid;
	if (!(pid = fork())) {
		if (DEBUG) {
			cout << "[" << getpid() << "] in child after fork()" << endl;
		}
		// argv of execvp() is not const char * const * due to
		// historical reasons, according to
		// https://stackoverflow.com/questions/10456043/why-is-argv-parameter-to-execvp-not-const
		// so we'll have to const_cast result of c_str() to char *
		// also, don't forget to prepend file as argv[0]
		char * argv[cmd.args.size() + 2];
		argv[0] = const_cast<char *>(cmd.file.c_str());
		for (size_t i = 0; i < cmd.args.size(); ++i) {
			argv[i + 1] = const_cast<char *>(cmd.args[i].c_str());
		}
		argv[cmd.args.size() + 1] = nullptr;
		execvp(cmd.file.c_str(), argv);
		// execvp() will only return on error
		cout << "execvp failed with error: " << strerror(errno) << endl;
	} else {
		if (DEBUG) {
			cout << "[" << getpid() << "] in parent after fork()" << endl;
		}
		// since we only have one child process, wait for it
		wait(nullptr);
		if (DEBUG) {
			cout << "[" << getpid() << "] after wait()" << endl;
		}
	}
}

bool repl_loop(istream &in) {
	cout << "> ";
	string input;
	getline(in, input);
	auto cmd = parse_input(input);
	if (cmd.file == "exit"s) {
		cout << "exiting!" << endl;
		return false;
	}
	exec_command(cmd);
	return true;
}
```

Above code added `exec_command()` , which executes the command we’ve parsed. After `fork()` , there will temporarily be two processes - a parent and a child - running the same code, which we will distinguish with the return value of `fork()` .

The child process will construct `argv` as required by the `execvp` system call, then making the call. If the call succeeds, `execvp` will not return and continue to run as the program we need until termination. Or, if something fails (like the requested executable cannot be found), `execvp()` returns and we print the error **and exit**.

The parent process uses `wait()` to wait for child process to exit, after which it will go on another `repl_loop()` . `wait()` actually waits for any child process, that’s fine since we now only have one child process at a time, but things would be different when we introduce job controls.

Additionally, we added a `DEBUG` constant, controlling whether the shell should print debug messages.

Now compile and run the code, we get:

```
$ ./a.out
> ls
[1071662] command is:
	file = ls
	args =
[1071662] in parent after fork()
[1071772] in child after fork()
a.out  main.cpp
[1071662] after wait()
> whoami
[1071662] command is:
	file = whoami
	args =
[1071662] in parent after fork()
[1071942] in child after fork()
std4453
[1071662] after wait()
> cat
[1071662] command is:
	file = cat
	args =
[1071662] in parent after fork()
[1099408] in child after fork()
hi
hi
hello
hello
[1071662] after wait()
> not_exist
[1071662] command is:
	file = not_exist
	args =
[1071662] in parent after fork()
[1072167] in child after fork()
execvp failed with error: No such file or directory
[1071662] after wait()
> exit
exiting!
```

That looks pretty much like a working shell already, although in the depth of our heart we know that it’s miles from complete.

Meanwhile, it should be interesting to notice that the error message printed when executing `not_exist` , the _No such file or directory_, is not written in our code. You might have seen that with `bash` when having misspelled the command, and now you know that it’s not provided by `bash` either.

## second, signal handling 101

One major difference between our shell and `bash` is handling of signals, see:

```
$ ./a.out
> cat
[1173010] command is:
	file = cat
	args =
[1173010] in parent after fork()
[1173086] in child after fork()
hi
hi
^C
$
```

When we execute an interactive program like `cat` and press `Ctrl+C` (or `Command+C` ), a `SIGINT` is sent to our shell, stopping it directly (the `$` after `^C` is from the shell that starts our shell).

Whereas normally when using a shell, we would expect `SIGINT` to stop the command that it’s running, but then return to the shell prompt instead of stopping the whole shell, like with `bash` :

```
$ cat
hi
hi
^C
$
```

<details>
  <summary>Advanced topic: Why <code>SIGINT</code>?</summary>

If somewhat experienced with using a Linux system, you might have formed the idea that whenever you want to interrupt a running program, you press `Ctrl+C` . Actually, the route from making the keystroke to the program stopping is not at straightforward.
Here, we assume that you’re using a terminal on a Linux-based desktop system.
First, your operating system informs your terminal of you pressing `Ctrl+C` , via the X protocol or Wayland.
After that, the terminal sends the [character ETX](https://en.wikipedia.org/wiki/End-of-Text_character) through the **pty** of your system. Why `Ctrl+C` is ETX? Because in ASCII, character C has code `0x43` , while ETX has `0x03` . For any character, `Ctrl+<char>` (commonly represented by `^<char>` since there chars are not printable) will result in sending `<char> XOR 0x40` .
**pty** stands for _pseudoterminal_, which is something within Linux to simulate a physical cable used historically for connecting a terminal machine. Since only characters get transferred along this cable, the system has a mechanic to map special characters to control signals, and this was also inherited to **pty**s.
The mapping is configurable, you can see that by running `stty -a` :

```
$ stty -a
speed 38400 baud; rows 47; columns 89; line = 0;
intr = ^C; quit = ^\; erase = ^?; kill = ^U; eof = ^D; eol = <undef>; eol2 = <undef>;
swtch = <undef>; start = ^Q; stop = ^S; susp = ^Z; rprnt = ^R; werase = ^W; lnext = ^V;
discard = ^O; min = 1; time = 0;
-parenb -parodd -cmspar cs8 -hupcl -cstopb cread -clocal -crtscts
-ignbrk -brkint -ignpar -parmrk -inpck -istrip -inlcr -igncr icrnl -ixon -ixoff -iuclc
ixany imaxbel iutf8
opost -olcuc -ocrnl onlcr -onocr -onlret -ofill -ofdel nl0 cr0 tab0 bs0 vt0 ff0
isig icanon iexten echo echoe echok -echonl -noflsh -xcase -tostop -echoprt echoctl echoke
-flusho -extproc
```

The part we care here is `intr = ^C` , which means that a `^C` character is mapped to an interruption. Upon receiving such an control character, your OS knows that a `SIGINT` should be sent.

</details>

Why isn’t that happening with our shell as well? Because we’re not instructing our shell to do that.

One might imagine the system to send `SIGINT` to the foreground program, `cat` , but there is actually no foreground program, only **foreground programs**.

Basically, several programs can run in foreground at the same time, it’s only we know that we won’t do anything until the running one exits, which, the system has no knowledge about.

So, when coming to sending `SIGINT` to the foreground program, it would actually send it to both our shell and the program we’re running. `cat` , as expected, exits upon receiving this signal, but unexpectedly, our shell exits as well.

It exits because for a plain C++ program, every singal is handled in the system-default way, and by looking at [signal(7)](https://man7.org/linux/man-pages/man7/signal.7.html), we can see that the default behavior with `SIGINT` is to terminate the program.

So the solution is simple, we ignore `SIGINT` when we’re running another program, by:

```cpp
void exec_command() {
  // ...
  if (!(pid = fork())) {
    // ...
  } else {
    // we cannot ignore SIGINT before fork() since execve() inherits
    // signal handlers set to SIG_DFL or SIG_IGN
    signal(SIGINT, SIG_IGN);
    // since we only have one child process, wait for it
    wait(nullptr);
    signal(SIGINT, SIG_DFL);
    // ...
  }
}
```

Now the program should work as we expected:

```
$ ./a.out
[566164] command is:
  file = cat
  args =
[566164] in parent after fork()
[566315] in child after fork()
hi
hi
^C[566164] after wait()
>
```

A side-effect of ignoring `SIGINT` it that when the shell is executing another program **non-interactively**, sending a `SIGINT` to the shell directly won’t work - neither the shell nor the running program is interrupted.

You can verify that with `bash` , like:

```
$ bash -c 'sleep 1000; sleep 1000' &
[1] 735346
$ ps auf
USER         PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME COMMAND
std4453   636139  0.0  0.0  11408  4996 pts/0    S<   15:19   0:00 bash
std4453   735346  0.1  0.0  10628  2920 pts/0    S<   15:40   0:00  \_ bash -c sleep
std4453   735347  0.0  0.0   8716  1052 pts/0    S<   15:40   0:00  |   \_ sleep 1000
std4453   735945  0.0  0.0  14808  4232 pts/0    R<+  15:40   0:00  \_ ps auf
$ kill -INT 735346
$ ps auf
USER         PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME COMMAND
std4453   636139  0.0  0.0  11408  4996 pts/0    S<   15:19   0:00 bash
std4453   735346  0.0  0.0  10628  2920 pts/0    S<   15:40   0:00  \_ bash -c sleep
std4453   735347  0.0  0.0   8716  1052 pts/0    S<   15:40   0:00  |   \_ sleep 1000
std4453   738808  0.0  0.0  14808  4332 pts/0    R<+  15:41   0:00  \_ ps auf
$ kill -INT 735347
$ ps auf
USER         PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME COMMAND
std4453   636139  0.0  0.0  11408  4996 pts/0    S<   15:19   0:00 bash
std4453   739231  0.0  0.0  14808  4308 pts/0    R<+  15:41   0:00  \_ ps auf
[1]+  Interrupt               bash -c 'sleep 1000; sleep 1000'
$
```

Actually, what you should be doing to imitate system behaviour when hitting `Ctrl+C` is to invoke `kill -INT -<pid_of_bash>` , the preceding `-` here means sending signal to whole **process group**.

Process group is a big topic but that’s for another day.

For now, you can play with our hand-made shell a bit more, looking for more differences between it and a production-level shell as `bash`.

The complete code of this part is at [`main.cpp`](./main.cpp), PRs are welcome.

Have fun!

