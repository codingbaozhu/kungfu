import './setEnv';
import { createApp, Component } from 'vue';
import { Subject } from 'rxjs';
import App from '@renderer/pages/index/App.vue';
import router from '@renderer/pages/index/router';
import store from '@renderer/pages/index/store';
import {
    Layout,
    Tabs,
    Button,
    Menu,
    Card,
    Input,
    Table,
    Switch,
    ConfigProvider,
    Modal,
    Radio,
    Tag,
    Form,
    InputNumber,
    Select,
    Drawer,
} from 'ant-design-vue';

import {
    beforeStartAll,
    getUIComponents,
} from '@renderer/assets/methods/uiUtils';
import { useGlobalStore } from '@renderer/pages/index/store/global';
import { delayMilliSeconds } from '@kungfu-trader/kungfu-js-api/utils/busiUtils';
import {
    Pm2ProcessStatusDetailData,
    Pm2ProcessStatusData,
    Pm2ProcessStatusTypes,
    startArchiveMakeTask,
    startGetProcessStatus,
    startLedger,
    startMaster,
} from '@kungfu-trader/kungfu-js-api/utils/processUtils';

const app = createApp(App);

const uics = getUIComponents();
Object.keys(uics).forEach((key) => {
    app.component(key, uics[key] as Component);
});

app.use(store)
    .use(router)
    .use(Layout)
    .use(Tabs)
    .use(Button)
    .use(Menu)
    .use(Card)
    .use(Input)
    .use(Table)
    .use(Switch)
    .use(ConfigProvider)
    .use(Modal)
    .use(Radio)
    .use(Tag)
    .use(InputNumber)
    .use(Select)
    .use(Drawer)
    .use(Form);

//this sort ensure $useGlobalStore can be get in mounted callback
app.config.globalProperties.$registedKfUIComponents = Object.keys(uics);
app.config.globalProperties.$bus = new Subject();
app.config.globalProperties.$useGlobalStore = useGlobalStore;

app.mount('#app');

const globalStore = useGlobalStore();

if (process.env.RELOAD_AFTER_CRASHED === 'false') {
    beforeStartAll()
        .then(() => {
            return startArchiveMakeTask(
                (archiveStatus: Pm2ProcessStatusTypes) => {
                    window.archiveStatus = archiveStatus;
                },
            );
        })
        .then(() => startMaster(false))
        .catch((err) => console.error(err.message))
        .finally(() => {
            startGetProcessStatus(
                (res: {
                    processStatus: Pm2ProcessStatusData;
                    processStatusWithDetail: Pm2ProcessStatusDetailData;
                }) => {
                    const { processStatus, processStatusWithDetail } = res;
                    globalStore.setProcessStatus(processStatus);
                    globalStore.setProcessStatusWithDetail(
                        processStatusWithDetail,
                    );
                },
            );

            delayMilliSeconds(1000)
                .then(() => startLedger(false))
                .catch((err) => console.error(err.message));
        });
} else {
    // 崩溃后重开，跳过archive过程
    window.archiveStatus = 'waiting restart';
    startGetProcessStatus(
        (res: {
            processStatus: Pm2ProcessStatusData;
            processStatusWithDetail: Pm2ProcessStatusDetailData;
        }) => {
            const { processStatus, processStatusWithDetail } = res;
            globalStore.setProcessStatus(processStatus);
            globalStore.setProcessStatusWithDetail(processStatusWithDetail);
        },
    );
}
