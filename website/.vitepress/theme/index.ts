import DefaultTheme from 'vitepress/theme'
import PipelineDemo from '../../components/PipelineDemo.vue'
import type { Theme } from 'vitepress'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('PipelineDemo', PipelineDemo)
  },
} satisfies Theme
