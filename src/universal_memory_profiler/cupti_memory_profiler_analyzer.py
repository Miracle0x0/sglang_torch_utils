# ---
# jupyter:
#   jupytext:
#     formats: ipynb,py:percent
#     text_representation:
#       extension: .py
#       format_name: percent
#       format_version: '1.3'
#       jupytext_version: 1.16.4
#   kernelspec:
#     display_name: Python 3 (ipykernel)
#     language: python
#     name: python3
# ---

# %%
from pathlib import Path
import polars as pl
import plotly.express as px
from tqdm.auto import tqdm

p = Path('/Users/tom/temp/temp_sglang_server2local/cupti_memory_profiler_1747922193.25324.log')

# %% [markdown]
# # Read

# %%
# lines = list(p.read_text().strip().split('\n'))
# df_raw = pl.DataFrame(dict(line=lines))
# print(f'{df_raw.estimated_size() / 1e9=}')
# df_raw

# %%
rows_activity, rows_callback = [], []
for line in tqdm(p.read_text().split('\n')):
    # print(line)
    if line:
        row = {}
        for chunk in line.split('\t'):
            [k, *vs] = chunk.split('=')
            row[k] = '='.join(vs)
        {
            "ACTIVITY_MEMORY": rows_activity,
            "CALLBACK": rows_callback,
        }[row.pop("category")].append(row)

# %%
df_activity_raw = pl.DataFrame(rows_activity, infer_schema_length=100000000)
df_callback_raw = pl.DataFrame(rows_callback, infer_schema_length=100000000)

# %%
df_activity = df_activity_raw.with_columns([
    pl.col(x).cast(int)
    for x in ['correlationId', 'address', 'size', 'deviceId', 'contextId', 'streamId']
])
df_activity = df_activity.with_columns(pl.col('timestamp').cast(int).cast(pl.Datetime("ns")))
df_activity = df_activity.with_columns(signed_size=pl.col('size') * (pl.when(pl.col('type') == 'ALLOC').then(+1).otherwise(-1)))

freed_addresses = df_activity.filter(pl.col('type') == 'FREE')['address'].to_list()
df_activity = df_activity.with_columns(is_later_freed=(pl.col('type') == 'ALLOC') & (pl.col('address').is_in(freed_addresses)))

df_activity = df_activity.with_columns(cum_size_gb=pl.col('signed_size').cum_sum().over('kind') / 1024**3)

df_callback = df_callback_raw.with_columns([
    pl.col(x).cast(int)
    for x in ['correlationId', 'domain', 'cbid']
])
df_callback = df_callback.with_columns(pl.col('cpp_stack').str.replace_all('[NL]', '\n', literal=True))

df_activity = df_activity.join(
    df_callback.filter(pl.col('site') == 'ENTER').select('correlationId', 'domain', 'cbid', 'apiName', 'cpp_stack', 'python_stack'),
    on='correlationId',
    how='left',
)

display(df_activity)
display(df_callback)
px.line(df_activity.select('timestamp', 'cum_size_gb', 'kind').to_pandas(), x='timestamp', y='cum_size_gb', color='kind').show()

# %% [markdown]
# # Analyze

# %% [markdown]
# ## Non-freed memory

# %%
df = df_activity.filter((pl.col('type') == 'ALLOC') & ~pl.col('is_later_freed'))
df = df.with_columns(
    pl.col('apiName')
        .fill_null(pl.format('{}-{}', 'domain', 'cbid'))
        # https://gitlab.com/nvidia/headers/cuda-individual/cupti/-/blob/main/cupti_callbacks.h
        #     -> CUPTI_CB_DOMAIN_DRIVER_API=1, CUPTI_CB_DOMAIN_RUNTIME_API=2
        # https://gitlab.com/nvidia/headers/cuda-individual/cupti/-/blob/main/cupti_driver_cbid.h
        # https://gitlab.com/nvidia/headers/cuda-individual/cupti/-/blob/main/cupti_runtime_cbid.h
        .replace({
            "1-243": 'cuMemAlloc_v2',
            "1-553": 'cuMemSetAccess',
            "2-179": 'cudaIpcOpenMemHandle',
            "1-567": 'cuIpcOpenMemHandle_v2',
        })
)
df = df.with_columns(cum_size_gb=pl.col('signed_size').cum_sum().over('kind', 'apiName') / 1024**3)
df = df.filter(pl.col('kind') == 'DEVICE')

# display(df)
# display(df.sort('size', descending=True))

display(df.group_by('apiName').agg(pl.col('size').sum()).sort('size', descending=True))

px.line(df.to_pandas(), x='timestamp', y='cum_size_gb', color='apiName').show()

# print(df[0].to_dicts()[0]['cpp_stack'])

df = df.with_columns(
    cpp_stack_interest=pl.col('cpp_stack').str.split('\n').list.eval(
        pl.element()
            .filter(pl.element().str.contains('nccl'))
            .str.extract(r' in (\w+)')
    ).list.join(" - ")
)
df = df.group_by('cpp_stack_interest', 'apiName').agg(pl.col('size').sum()).sort('size', descending=True)
with pl.Config(fmt_str_lengths=10000, tbl_rows=10000):
    display(df)

# df = df.filter(~pl.col('cpp_stack').str.contains('libnccl'))
# display(df)
# print(df[0].to_dicts()[0]['cpp_stack'])
