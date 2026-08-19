#include "ui/MainComponent.h"

namespace rvctuner
{
/** @brief The application: one window, one recording at a time. */
class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "RVCTuner"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        window = std::make_unique<Window>();

        const auto arguments = juce::StringArray::fromTokens (commandLine, true);

        for (const auto& argument : arguments)
        {
            const juce::File file { argument.unquoted() };

            if (file.existsAsFile())
            {
                window->getMainComponent().open (file);
                break;
            }
        }
    }

    void shutdown() override { window.reset(); }

    void systemRequestedQuit() override { quit(); }

private:
    /** @brief The document window, which owns the editor. */
    class Window final : public juce::DocumentWindow
    {
    public:
        Window()
            : juce::DocumentWindow ("RVCTuner",
                                    PanelLookAndFeel::Palette::ground,
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);
            setResizeLimits (1000, 640, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        [[nodiscard]] MainComponent& getMainComponent()
        {
            return *dynamic_cast<MainComponent*> (getContentComponent());
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Window)
    };

    std::unique_ptr<Window> window;
};
}

START_JUCE_APPLICATION (rvctuner::Application)
