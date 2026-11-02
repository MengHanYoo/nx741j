/* assetsdwrap: run assetsd with stdout/stderr on the kernel console.
 * The port logs console writes as "|proc| ...", so libc++abi's abort message
 * (which only goes to stderr) becomes visible in the kernel log.
 * Started through sweetexecswap=assetsd:/usr/libexec/assetsdwrap.
 */
#include <fcntl.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	static const char real[] =
	    "/System/Library/Frameworks/AssetsLibrary.framework/Support/assetsd";
	int fd = open("/dev/console", O_WRONLY | O_APPEND);

	if (fd >= 0) {
		dup2(fd, 1);
		dup2(fd, 2);
		if (fd > 2) {
			close(fd);
		}
	}
	if (argc > 0) {
		argv[0] = (char *)real;
	}
	execv(real, argv);
	return 127;
}
