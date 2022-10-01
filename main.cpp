#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

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
		// don't forget to exit child process 
		exit(-1);
		return;
	} else {
		if (DEBUG) {
			cout << "[" << getpid() << "] in parent after fork()" << endl;
		}
		// we cannot ignore SIGINT before fork() since execve() inherits
		// signal handlers set to SIG_DFL or SIG_IGN
		signal(SIGINT, SIG_IGN);
		// since we only have one child process, wait for it 
		wait(nullptr);
		signal(SIGINT, SIG_DFL);
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
