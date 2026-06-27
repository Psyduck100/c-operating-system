

case SYS_EXIT:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args);
    if (!success) thread_exit();
    exit(args[0]);
    break;

case SYS_EXEC:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args);
    if (!success) thread_exit();
    if (!check_file_pointer((char *)args[0])) thread_exit();
    f->eax = exec((char *)args[0]);
    break;

case SYS_WAIT:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args);
    if (!success) thread_exit();
    f->eax = wait((pid_t)args[0]);
    break;

case SYS_CREATE:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args * 2);
    if (!success) thread_exit();
    if (!check_file_pointer((char *)args[0])) thread_exit();
    f->eax = create((char *)args[0], (unsigned)args[1]);
    break;

case SYS_REMOVE:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args);
    if (!success) thread_exit();
    if (!check_file_pointer((char *)args[0])) thread_exit();
    f->eax = remove((char *)args[0]);
    break;

case SYS_OPEN:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args);
    if (!success) thread_exit();
    if (!check_file_pointer((char *)args[0])) thread_exit();
    f->eax = open((char *)args[0]);
    break;

case SYS_FILESIZE:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args);
    if (!success) thread_exit();
    f->eax = filesize(args[0]);
    break;

case SYS_READ:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args * 3);
    if (!success) thread_exit();
    if (!check_file_pointer((char *)args[1])) thread_exit();
    f->eax = read(args[0], (void *)args[1], (unsigned)args[2]);
    break;

case SYS_WRITE:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args * 3);
    if (!success) thread_exit();
    if (!check_file_pointer((char *)args[1])) thread_exit();
    f->eax = write(args[0], (void *)args[1], (unsigned)args[2]);
    break;

case SYS_SEEK:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args * 2);
    if (!success) thread_exit();
    seek(args[0], (unsigned)args[1]);
    break;

case SYS_TELL:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args);
    if (!success) thread_exit();
    f->eax = tell(args[0]);
    break;

case SYS_CLOSE:
    success = copy_in(args, (uint32_t *)f->esp + 1, sizeof *args);
    if (!success) thread_exit();
    close(args[0]);
    break;