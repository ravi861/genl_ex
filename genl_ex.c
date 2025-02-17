#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netlink/msg.h>
#include <netlink/attr.h>

#include "genl_ex.h"

static char message[GENL_TEST_ATTR_MSG_MAX];
static int send_to_kernel;

static void usage(char *name) {
  printf(
      "Usage: %s\n"
      "	-h : this help message\n"
      "	-m : the message to send\n",
      name);
}

static void parse_cmd_line(int argc, char **argv) {
  while (1) {
    int opt = getopt(argc, argv, "hl:m:s");

    if (opt == EOF) break;

    switch (opt) {
      case 'h':
        usage(argv[0]);
        exit(EXIT_SUCCESS);

      case 'm':
        strncpy(message, optarg, GENL_TEST_ATTR_MSG_MAX);
        message[GENL_TEST_ATTR_MSG_MAX - 1] = '\0';
        send_to_kernel = 1;
        break;

      default:
        fprintf(stderr, "Unkown option %c !!\n", opt);
        exit(EXIT_FAILURE);
    }
  }
}
static int send_msg_to_kernel(struct nl_sock *sock) {
  struct nl_msg *msg;
  int family_id, err = 0;

  family_id = genl_ctrl_resolve(sock, GENL_TEST_FAMILY_NAME);
  if (family_id < 0) {
    fprintf(stderr, "Unable to resolve family name!\n");
    exit(EXIT_FAILURE);
  }

  msg = nlmsg_alloc();
  if (!msg) {
    fprintf(stderr, "failed to allocate netlink message\n");
    exit(EXIT_FAILURE);
  }

  if (!genlmsg_put(msg,
                   NL_AUTO_PID,
                   NL_AUTO_SEQ,
                   family_id,
                   0,
                   NLM_F_REQUEST,
                   GENL_TEST_C_MSG,
                   0)) {
    fprintf(stderr, "failed to put nl hdr!\n");
    err = -ENOMEM;
    goto out;
  }

  err = nla_put_string(msg, GENL_TEST_ATTR_MSG, message);
  if (err) {
    fprintf(stderr, "failed to put nl string!\n");
    goto out;
  }

  err = nl_send(sock, msg);
  if (err < 0) {
    fprintf(stderr, "failed to send nl message %d!\n", err);
  }
  fprintf(stdout, "sent nl message %d\n", err);

out:
  nlmsg_free(msg);
  return err;
}

static int print_rx_msg(struct nl_msg *msg, void *arg) {
  struct nlattr *attr[GENL_TEST_ATTR_MAX + 1];

  genlmsg_parse(nlmsg_hdr(msg), 0, attr, GENL_TEST_ATTR_MAX, genl_test_policy);

  if (!attr[GENL_TEST_ATTR_MSG]) return NL_OK;

  fprintf(
      stdout, "Kernel says: %s \n", nla_get_string(attr[GENL_TEST_ATTR_MSG]));

  return NL_OK;
}

static void prep_nl_sock(struct nl_sock **nlsock) {
  int family_id, grp_id;
  unsigned int bit = 0;

  *nlsock = nl_socket_alloc();
  if (!*nlsock) {
    fprintf(stderr, "Unable to alloc nl socket!\n");
    exit(EXIT_FAILURE);
  }
  (void)genl_test_mcgrp_names;

  /* disable seq checks on multicast sockets */
  nl_socket_disable_seq_check(*nlsock);
  nl_socket_disable_auto_ack(*nlsock);

  /* connect to genl */
  if (genl_connect(*nlsock)) {
    fprintf(stderr, "Unable to connect to genl!\n");
    goto exit_err;
  }

  /* resolve the generic nl family id*/
  family_id = genl_ctrl_resolve(*nlsock, GENL_TEST_FAMILY_NAME);
  if (family_id < 0) {
    fprintf(stderr, "Unable to resolve family name!\n");
    goto exit_err;
  }

  grp_id = genl_ctrl_resolve_grp(
      *nlsock, GENL_TEST_FAMILY_NAME, genl_test_mcgrp_names[0]);

  if (grp_id < 0) {
    fprintf(stderr, "Unable to resolve group name for %u!\n", (1 << bit));
    goto exit_err;
  }
  if (nl_socket_add_membership(*nlsock, grp_id)) {
    fprintf(stderr, "Unable to join group %u!\n", (1 << bit));
    goto exit_err;
  }
  nl_socket_set_peer_groups(*nlsock, 1 << (grp_id - 1));

  return;

exit_err:
  nl_socket_free(*nlsock);  // this call closes the socket as well
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
  struct nl_sock *nlsock = NULL;
  struct nl_cb *cb = NULL;
  int ret;

  parse_cmd_line(argc, argv);

  prep_nl_sock(&nlsock);

  if (send_to_kernel) {
    ret = send_msg_to_kernel(nlsock);
    exit(EXIT_SUCCESS);
  }

  /* prep the cb */
  cb = nl_cb_alloc(NL_CB_DEFAULT);
  nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, print_rx_msg, NULL);
  do {
    ret = nl_recvmsgs(nlsock, cb);
  } while (!ret);

  nl_cb_put(cb);
  nl_socket_free(nlsock);
  return 0;
}
