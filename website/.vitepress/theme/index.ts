import DefaultTheme from 'vitepress/theme'
import PipelineDemo from '../../components/PipelineDemo.vue'
import ArmStepDemo from '../../components/ArmStepDemo.vue'
import type { Theme } from 'vitepress'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('PipelineDemo', PipelineDemo)
    app.component('ArmStepDemo', ArmStepDemo)
  },
} satisfies Theme
