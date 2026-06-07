import DefaultTheme from 'vitepress/theme'
import PipelineDemo from '../../components/PipelineDemo.vue'
import ArmStepDemo from '../../components/ArmStepDemo.vue'
import ThumbAddDemo from '../../components/ThumbAddDemo.vue'
import MemoryMapDemo from '../../components/MemoryMapDemo.vue'
import EventQueueDemo from '../../components/EventQueueDemo.vue'
import DmaTransferDemo from '../../components/DmaTransferDemo.vue'
import ScanlineDemo from '../../components/ScanlineDemo.vue'
import type { Theme } from 'vitepress'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('PipelineDemo', PipelineDemo)
    app.component('ArmStepDemo', ArmStepDemo)
    app.component('ThumbAddDemo', ThumbAddDemo)
    app.component('MemoryMapDemo', MemoryMapDemo)
    app.component('EventQueueDemo', EventQueueDemo)
    app.component('DmaTransferDemo', DmaTransferDemo)
    app.component('ScanlineDemo', ScanlineDemo)
  },
} satisfies Theme
