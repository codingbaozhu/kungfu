from pykungfu import longfist
from pykungfu import yijinjing as yjj
import click
from kungfu.command import kfc, pass_ctx_from_parent
from kungfu_extensions import EXTENSION_REGISTRY_TD


@kfc.command(help_priority=3)
@click.option('-s', '--source', required=True, type=click.Choice(EXTENSION_REGISTRY_TD.names()), help='destination to send order')
@click.option('-a', '--account', type=str, help='account')
@click.option('-x', '--low_latency', is_flag=True, help='run in low latency mode')
@click.pass_context
def td(ctx, source, account, low_latency):
    pass_ctx_from_parent(ctx)
    config = yjj.location(yjj.mode.LIVE, yjj.category.TD, source, account, ctx.locator).to(longfist.types.Config())
    config = yjj.profile(ctx.locator).get(config)
    ext = EXTENSION_REGISTRY_TD.get_extension(source)(low_latency, ctx.locator, account, config.value)
    ext.run()
