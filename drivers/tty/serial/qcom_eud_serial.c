// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm SDM845 Embedded USB Debugger COM serial driver.
 */

#include <linux/bitops.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#define EUD_REG_COM_TX_ID	0x0000
#define EUD_REG_COM_TX_LEN	0x0004
#define EUD_REG_COM_TX_DAT	0x0008
#define EUD_REG_CSR_EUD_EN	0x1014

#define EUD_ENABLE		BIT(0)
#define EUD_COM_APPS_ID		0x90
#define EUD_FIFO_SIZE		14
#define EUD_NR_PORTS		1
#define EUD_FRAME_DELAY_US	1000
#define EUD_UARTCLK		(115200 * 16)

struct qcom_eud_port {
	struct uart_port port;
	struct work_struct tx_work;
	bool tx_enabled;
};

static struct uart_driver qcom_eud_uart_driver;

static inline struct qcom_eud_port *to_eud_port(struct uart_port *port)
{
	return container_of(port, struct qcom_eud_port, port);
}

static int qcom_eud_write_frame(void __iomem *base, const unsigned char *buf,
				unsigned int count)
{
	unsigned int i;
	u32 reg;

	if (!count || count > EUD_FIFO_SIZE)
		return -EINVAL;

	writel(EUD_COM_APPS_ID, base + EUD_REG_COM_TX_ID);
	reg = readl(base + EUD_REG_COM_TX_ID);
	if ((reg & 0xff) != EUD_COM_APPS_ID)
		return -EIO;

	for (i = 0; i < count; i++)
		writel(buf[i], base + EUD_REG_COM_TX_DAT);

	writel(count, base + EUD_REG_COM_TX_LEN);

	return 0;
}

static void qcom_eud_tx_work(struct work_struct *work)
{
	struct qcom_eud_port *eud = container_of(work, struct qcom_eud_port,
						 tx_work);
	struct uart_port *port = &eud->port;
	unsigned char buf[EUD_FIFO_SIZE];
	bool more;
	int ret;

	do {
		struct tty_port *tport;
		unsigned int count;
		unsigned int pending = 0;
		unsigned long flags;
		bool x_char = false;

		more = false;
		ret = 0;

		uart_port_lock_irqsave(port, &flags);

		if (!eud->tx_enabled || !port->state || uart_tx_stopped(port))
			goto out_unlock;

		if (port->x_char) {
			buf[0] = port->x_char;
			count = 1;
			x_char = true;
		} else {
			tport = &port->state->port;
			count = kfifo_out_peek(&tport->xmit_fifo, buf,
					       sizeof(buf));
		}

		if (!count) {
			eud->tx_enabled = false;
			goto out_unlock;
		}

		ret = qcom_eud_write_frame(port->membase, buf, count);
		if (ret) {
			eud->tx_enabled = false;
			goto out_unlock;
		}

		if (x_char) {
			port->x_char = 0;
			port->icount.tx++;
		} else {
			tport = &port->state->port;
			uart_xmit_advance(port, count);
			pending = kfifo_len(&tport->xmit_fifo);
			if (pending < WAKEUP_CHARS)
				uart_write_wakeup(port);
		}

		if (port->x_char)
			more = true;
		else if (port->state)
			more = !!kfifo_len(&port->state->port.xmit_fifo);

		if (!more)
			eud->tx_enabled = false;

out_unlock:
		uart_port_unlock_irqrestore(port, flags);

		if (ret)
			dev_err_ratelimited(port->dev,
					    "failed to write COM frame: %d\n",
					    ret);

		if (more)
			usleep_range(EUD_FRAME_DELAY_US,
				     EUD_FRAME_DELAY_US + 1000);
	} while (more);
}

static unsigned int qcom_eud_tx_empty(struct uart_port *port)
{
	return TIOCSER_TEMT;
}

static void qcom_eud_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static unsigned int qcom_eud_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_CTS | TIOCM_DSR;
}

static void qcom_eud_stop_tx(struct uart_port *port)
{
	struct qcom_eud_port *eud = to_eud_port(port);

	eud->tx_enabled = false;
}

static void qcom_eud_start_tx(struct uart_port *port)
{
	struct qcom_eud_port *eud = to_eud_port(port);

	eud->tx_enabled = true;
	schedule_work(&eud->tx_work);
}

static void qcom_eud_stop_rx(struct uart_port *port)
{
}

static int qcom_eud_startup(struct uart_port *port)
{
	writel(EUD_ENABLE, port->membase + EUD_REG_CSR_EUD_EN);

	return 0;
}

static void qcom_eud_shutdown(struct uart_port *port)
{
	struct qcom_eud_port *eud = to_eud_port(port);
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	eud->tx_enabled = false;
	uart_port_unlock_irqrestore(port, flags);

	cancel_work_sync(&eud->tx_work);
}

static void qcom_eud_set_termios(struct uart_port *port,
				 struct ktermios *termios,
				 const struct ktermios *old)
{
	termios->c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
	termios->c_cflag |= CS8 | CLOCAL;
	tty_termios_encode_baud_rate(termios, 115200, 115200);
	uart_update_timeout(port, termios->c_cflag, 115200);
}

static const char *qcom_eud_type(struct uart_port *port)
{
	return port->type == PORT_GENERIC ? "Qualcomm EUD COM" : NULL;
}

static void qcom_eud_release_port(struct uart_port *port)
{
}

static int qcom_eud_request_port(struct uart_port *port)
{
	return 0;
}

static void qcom_eud_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_GENERIC;
}

static int qcom_eud_verify_port(struct uart_port *port,
				struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_GENERIC)
		return -EINVAL;

	return 0;
}

static const struct uart_ops qcom_eud_uart_ops = {
	.tx_empty	= qcom_eud_tx_empty,
	.set_mctrl	= qcom_eud_set_mctrl,
	.get_mctrl	= qcom_eud_get_mctrl,
	.stop_tx	= qcom_eud_stop_tx,
	.start_tx	= qcom_eud_start_tx,
	.stop_rx	= qcom_eud_stop_rx,
	.startup	= qcom_eud_startup,
	.shutdown	= qcom_eud_shutdown,
	.set_termios	= qcom_eud_set_termios,
	.type		= qcom_eud_type,
	.release_port	= qcom_eud_release_port,
	.request_port	= qcom_eud_request_port,
	.config_port	= qcom_eud_config_port,
	.verify_port	= qcom_eud_verify_port,
};

#ifdef CONFIG_SERIAL_QCOM_EUD_EARLYCON
static void qcom_eud_early_putc(struct uart_port *port, unsigned char ch)
{
	qcom_eud_write_frame(port->membase, &ch, 1);
	udelay(EUD_FRAME_DELAY_US);
}

static void qcom_eud_early_write(struct console *con, const char *s,
				 unsigned int count)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, count, qcom_eud_early_putc);
}

static int __init qcom_eud_earlycon_setup(struct earlycon_device *dev,
					  const char *options)
{
	if (!dev->port.membase)
		return -ENODEV;

	dev->con->write = qcom_eud_early_write;

	return 0;
}

EARLYCON_DECLARE(qcom_eud, qcom_eud_earlycon_setup);
OF_EARLYCON_DECLARE(qcom_eud, "qcom,sdm845-eud",
		    qcom_eud_earlycon_setup);
#endif

static int qcom_eud_probe(struct platform_device *pdev)
{
	struct qcom_eud_port *eud;
	struct uart_port *port;
	struct resource *res;
	int ret;

	eud = devm_kzalloc(&pdev->dev, sizeof(*eud), GFP_KERNEL);
	if (!eud)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	port = &eud->port;
	port->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);

	port->dev = &pdev->dev;
	port->mapbase = res->start;
	port->mapsize = resource_size(res);
	port->iotype = UPIO_MEM;
	port->uartclk = EUD_UARTCLK;
	port->fifosize = EUD_FIFO_SIZE;
	port->flags = UPF_BOOT_AUTOCONF;
	port->line = 0;
	port->ops = &qcom_eud_uart_ops;

	spin_lock_init(&port->lock);
	INIT_WORK(&eud->tx_work, qcom_eud_tx_work);

	platform_set_drvdata(pdev, eud);

	ret = uart_add_one_port(&qcom_eud_uart_driver, port);
	if (ret)
		return ret;

	return 0;
}

static void qcom_eud_remove(struct platform_device *pdev)
{
	struct qcom_eud_port *eud = platform_get_drvdata(pdev);

	uart_remove_one_port(&qcom_eud_uart_driver, &eud->port);
	cancel_work_sync(&eud->tx_work);
}

static const struct of_device_id qcom_eud_of_match[] = {
	{ .compatible = "qcom,sdm845-eud" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_eud_of_match);

static struct platform_driver qcom_eud_platform_driver = {
	.probe	= qcom_eud_probe,
	.remove	= qcom_eud_remove,
	.driver	= {
		.name		= "qcom_eud_serial",
		.of_match_table	= qcom_eud_of_match,
	},
};

static struct uart_driver qcom_eud_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "qcom_eud_serial",
	.dev_name	= "ttyEUD",
	.nr		= EUD_NR_PORTS,
};

static int __init qcom_eud_init(void)
{
	int ret;

	ret = uart_register_driver(&qcom_eud_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&qcom_eud_platform_driver);
	if (ret)
		uart_unregister_driver(&qcom_eud_uart_driver);

	return ret;
}
module_init(qcom_eud_init);

static void __exit qcom_eud_exit(void)
{
	platform_driver_unregister(&qcom_eud_platform_driver);
	uart_unregister_driver(&qcom_eud_uart_driver);
}
module_exit(qcom_eud_exit);

MODULE_DESCRIPTION("Qualcomm SDM845 EUD COM serial driver");
MODULE_LICENSE("GPL");
