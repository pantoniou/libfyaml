// SPDX-License-Identifier: MIT
//
// Link with --pre-js, for a program that runs under node:
//
// - node does not give its environment to an Emscripten program, and the
//   program needs it (the tests set variables such as LIBCLANG_DUMP_COMMENTS);
// - node leaves the pipes of a program non-blocking, so that a write of more
//   than the pipe holds, to a reader that is slow, or a read of an empty pipe,
//   fails with EAGAIN. A buffered stdio stream cannot go on after that without
//   losing data, so the system call has to wait and try again, as a blocking
//   descriptor does.
Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
	/* Emscripten leaves ENV out of a program that does not use the environment */
	if (typeof ENV !== 'undefined')
		Object.assign(ENV, process.env);
});

if (typeof process !== 'undefined' && process.versions && process.versions.node) {
	var nodeFs = require('fs');
	var nodeSleep = function() {
		Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 1);
	};

	['writeSync', 'readSync'].forEach(function(name) {
		var orig = nodeFs[name];

		nodeFs[name] = function() {
			for (;;) {
				try {
					return orig.apply(nodeFs, arguments);
				} catch (e) {
					if (e.code !== 'EAGAIN')
						throw e;
					nodeSleep();
				}
			}
		};
	});
}
